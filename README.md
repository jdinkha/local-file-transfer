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

# Showcase
- Sending:
<img width="770" height="509" alt="_sending" src="https://github.com/user-attachments/assets/ab886398-2bcc-4a10-a6af-e925236292f5" />

- Receiving:
<img width="770" height="601" alt="_receiving" src="https://github.com/user-attachments/assets/ef0320d2-3920-4b3f-894d-7fe547aa2372" />
