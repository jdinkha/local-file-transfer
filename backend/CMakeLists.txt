cmake_minimum_required(VERSION 3.16)
project(FileTransferBackend)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(nlohmann_json)

add_executable(filetransfer_backend
    src/main.cpp
    src/FileTransferServer.cpp
    src/FileTransferClient.cpp
    src/NetworkDiscovery.cpp
    src/Protocol.cpp
)

target_link_libraries(filetransfer_backend
    pthread
)
