#include "FileDialog.hpp"

#include <cstdlib>
#include <vector>

#if defined(__linux__) && !defined(__APPLE__)
#include <algorithm>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#endif

namespace tannhauser {

std::string homeDirectory() {
#if defined(_WIN32)
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    return home ? home : "";
}

#if defined(__linux__) && !defined(__APPLE__)

static bool onPath(const char* program) {
    const char* path = std::getenv("PATH");
    if (!path) return false;
    std::string dirs(path);
    size_t start = 0;
    while (start <= dirs.size()) {
        const size_t end = std::min(dirs.find(':', start), dirs.size());
        const std::string file = dirs.substr(start, end - start) + "/" + program;
        if (end > start && access(file.c_str(), X_OK) == 0) return true;
        start = end + 1;
    }
    return false;
}

bool FileDialogProcess::start(const FileDialogOptions& options) {
    if (running()) return true;
    const std::string pattern = "*." + options.extension;
    const std::string described = options.filterName + " (" + pattern + ")";
    std::vector<std::string> args;
    if (onPath("zenity")) {
        args = { "zenity", "--file-selection", "--title=" + options.title,
                 "--file-filter=" + described + " | " + pattern, "--file-filter=All files | *",
                 "--filename=" + options.startPath };
        if (options.save) { args.push_back("--save"); args.push_back("--confirm-overwrite"); }
    } else if (onPath("kdialog")) {
        args = { "kdialog", options.save ? "--getsavefilename" : "--getopenfilename", options.startPath,
                 pattern + "|" + described, "--title", options.title };
    } else {
        return false;
    }
    int fds[2];
    if (pipe(fds) != 0) return false;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], 1);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    posix_spawn_file_actions_addclose(&actions, fds[1]);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(&a[0]);
    argv.push_back(nullptr);
    pid_t pid = 0;
    const int err = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    if (err != 0) { close(fds[0]); return false; }
    fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
    pid_ = pid;
    fd_ = fds[0];
    output_.clear();
    save_ = options.save;
    return true;
}

bool FileDialogProcess::poll(std::string& path, bool& save) {
    if (!running()) return false;
    char buf[512];
    ssize_t n;
    while ((n = read(fd_, buf, sizeof(buf))) > 0) output_.append(buf, static_cast<size_t>(n));
    int status = 0;
    if (waitpid(pid_, &status, WNOHANG) != pid_) return false;
    while ((n = read(fd_, buf, sizeof(buf))) > 0) output_.append(buf, static_cast<size_t>(n));
    close(fd_);
    pid_ = 0;
    fd_ = -1;
    path.clear();
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        path = output_.substr(0, output_.find('\n'));
    }
    save = save_;
    return true;
}

void FileDialogProcess::cancel() {
    if (!running()) return;
    kill(pid_, SIGTERM);
    waitpid(pid_, nullptr, 0);
    close(fd_);
    pid_ = 0;
    fd_ = -1;
}

#elif defined(_WIN32)

static std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::filesystem::path runFileDialog(void* owner, const FileDialogOptions& options) {
    const std::filesystem::path start = std::filesystem::u8path(options.startPath);
    std::wstring dir, name;
    std::error_code ec;
    if (!options.startPath.empty() && std::filesystem::is_directory(start, ec)) {
        dir = start.wstring();
    } else {
        dir = start.parent_path().wstring();
        name = start.filename().wstring();
    }
    std::vector<wchar_t> file(MAX_PATH, L'\0');
    if (name.size() < file.size()) std::copy(name.begin(), name.end(), file.begin());
    const std::wstring ext = widen(options.extension);
    std::wstring filter = widen(options.filterName) + L" (*." + ext + L")";
    filter.push_back(L'\0');
    filter += L"*." + ext;
    filter.push_back(L'\0');
    filter += L"All files (*.*)";
    filter.push_back(L'\0');
    filter += L"*.*";
    filter.push_back(L'\0');
    filter.push_back(L'\0');
    const std::wstring title = widen(options.title);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(owner);
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file.data();
    ofn.nMaxFile = static_cast<DWORD>(file.size());
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.lpstrDefExt = ext.c_str();
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (options.save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL ok = options.save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    return ok ? std::filesystem::path(file.data()) : std::filesystem::path();
}

#endif

} // namespace tannhauser
