#ifndef TANNHAUSER_FILE_DIALOG_HPP
#define TANNHAUSER_FILE_DIALOG_HPP

// Open / save file dialogs for the plugin GUIs (Acidus's calibration files,
// Burette's pattern banks).
//
// Linux: plain X11 has no file dialog, so zenity or kdialog runs as a child
// process whose output (the chosen path) the GUI's event loop polls; the GUI
// keeps running while the dialog is open. Windows: the common dialog, modal.

#include <filesystem>
#include <string>

namespace tannhauser {

struct FileDialogOptions {
    bool save{false};
    std::string title;
    std::string filterName;   // e.g. "Calibration profiles"
    std::string extension;    // without the dot, e.g. "json"
    std::string startPath;    // a directory, or a directory and file name
};

// The user's home directory (HOME, or USERPROFILE on Windows), or "".
std::string homeDirectory();

#if defined(__linux__) && !defined(__APPLE__)
class FileDialogProcess {
public:
    FileDialogProcess() = default;
    FileDialogProcess(const FileDialogProcess&) = delete;
    FileDialogProcess& operator=(const FileDialogProcess&) = delete;
    ~FileDialogProcess() { cancel(); }

    bool running() const { return pid_ > 0; }
    // False if neither zenity nor kdialog is installed or it could not start.
    bool start(const FileDialogOptions& options);
    // True once the dialog has closed: `path` is the chosen file, or empty
    // when it was cancelled. `save` tells which kind of dialog it was.
    bool poll(std::string& path, bool& save);
    void cancel();

private:
    int pid_{0};
    int fd_{-1};
    bool save_{false};
    std::string output_;
};
#elif defined(_WIN32)
// Modal. Empty if cancelled. `owner` is the parent HWND.
std::filesystem::path runFileDialog(void* owner, const FileDialogOptions& options);
#endif

} // namespace tannhauser

#endif // TANNHAUSER_FILE_DIALOG_HPP
