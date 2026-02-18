cmake_minimum_required(VERSION 3.16)
project(FileTransferBackend)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(nlohmann_json)

add_executable(filetransfer_backend
    src/main.cpp
    src/fileTransferServer.cpp
    src/fileTransferClient.cpp
    src/networkDiscovery.cpp
    src/protocol.cpp
)

target_include_directories(filetransfer_backend PRIVATE 
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(filetransfer_backend
    PRIVATE
    nlohmann_json::nlohmann_json
)

if(WIN32)
    # Windows needs Winsock library
    target_link_libraries(filetransfer_backend PRIVATE ws2_32)

    # Add Windows compatibility definitions
    target_compile_definitions(filetransfer_backend PRIVATE _WIN32_WINNT=0x0601)
else()
    # Unix-like systems need pthread for threading
    target_link_libraries(filetransfer_backend PRIVATE pthread)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
