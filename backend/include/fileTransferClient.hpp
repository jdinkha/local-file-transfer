#pragma once

#include <string>
#include <functional>  // For callback functions
#include <cstdint>
#include "protocol.hpp"

/**
 * FileTransferClient class handles sending files to a remote server
 * This is the sender side of the file transfer application
 */
class FileTransferClient {
private:
    int client_fd;           // Socket file descriptor
    std::string server_ip;   // IP address of the receiving device
    int port;
    bool connected;
    
    // Callback function for progress updates (using std::function for flexibility)
    // This allows the UI to show transfer progress
    std::function<void(int percentage, uint64_t transferred, uint64_t total)> progress_callback;

public:
    /**
     * Constructor - initializes the client with server details
     * @param ip: IP address of the receiving device
     * @param port: Port number (default: 5000)
     */
    FileTransferClient(const std::string& ip, int port = 5000);
    
	//Destructor - ensures socket is closed properly
    ~FileTransferClient();
    
    
     //Establishes TCP connection to the server
    bool connect();
    
    /**
     * Sends a file to the connected server
     * @param filepath: Path to the file to send
     * @return: true if transfer successful, false otherwise
     */
    bool sendFile(const std::string& filepath);
    
    /**
     * Closes the connection gracefully
     */
    void disconnect();
    
    /**
     * Sets a callback function to receive progress updates
     * @param callback: Function taking (percentage, transferred_bytes, total_bytes)
     */
    void setProgressCallback(std::function<void(int, uint64_t, uint64_t)> callback) {
        progress_callback = callback;
    }
    
    bool isConnected() const { return connected; }
};
