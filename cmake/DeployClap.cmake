# Copies the built .clap into the folder named by the CLAPTEST environment
# variable. Run in script mode (cmake -DCLAP_FILE=<path> -P DeployClap.cmake) so
# the variable is read when the target runs, not when CMake configured.
if(NOT DEFINED ENV{CLAPTEST} OR "$ENV{CLAPTEST}" STREQUAL "")
    message(FATAL_ERROR "CLAPTEST environment variable is not set (restart VS Code after setting it).")
endif()

if(NOT IS_DIRECTORY "$ENV{CLAPTEST}")
    message(FATAL_ERROR "CLAPTEST folder does not exist: $ENV{CLAPTEST}")
endif()

file(COPY "${CLAP_FILE}" DESTINATION "$ENV{CLAPTEST}")
message(STATUS "Deployed ${CLAP_FILE} -> $ENV{CLAPTEST}")
