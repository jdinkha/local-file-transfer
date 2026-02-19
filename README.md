# local-file-transfer
Application for unencrypted local file transfer between two devices/servers over a network

# Dependencies
- CMake 3.16+
- C++ 17

# Building/running
To build, clone the repo, cd into the 'backend' directory, then run:
- mkdir build && cd build
- cmake ..
- make -j4
- ./filetransfer_backend

(You can use -j3 or -j2 on older hardware)

# Future goals
- Frontend
- Mobile support
- Ask which directory to store files in
- Fix disconnection issue on receiving server
- Add encryption
- Add automatic discovery
- Add checksum verification
- Add directory transfer support
- Stress testing large files (sometimes sender closes connection before receiver is finished)
- Fix '~' directory issue
- Ask to overwrite/skip old files with same filename
- Change bytes to kb, mb, gb
