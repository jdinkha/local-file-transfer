#pragma once

#include <string>
#include <vector>
#include <thread>
#include <functional>
#include <cstdint>
#include <map>
#include <mutex>
#include "protocol.hpp"

/**
 * Structure to hold information about a connected client
 * Used to track multiple simultaneous connections
 */
struct ClientInfo {
	int socket_fd;                    // Client socket file descriptor
	std::string ip_address;           // Client IP address
	int port;                          // Client port
	std::thread* handler_thread;       // Thread handling this client
	bool is_active;                    // Whether client is still connected
	uint64_t bytes_received;           // For tracking transfer progress
	std::string current_filename;      // File being received from this client
};

/**
 * FileTransferServer class handles receiving files from multiple clients
 * This is the receiver side of the file transfer application
 */
class FileTransferServer {
private:
	int server_fd;                      // Server socket file descriptor
	int port;                            // Port to listen on
	bool is_running;                     // Server status flag
	std::vector<ClientInfo> clients;     // List of connected clients
	std::thread* accept_thread;          // Thread for accepting new connections
	std::mutex clients_mutex;             // Mutex for thread-safe client list access
	
	// Callback for notifying about received files
	std::function<void(const std::string& filename, uint64_t size)> file_received_callback;
	
	// Callback for progress updates
	std::function<void(const std::string& client_ip, int percentage)> progress_callback;
	
	/**
	 * Main server loop that accepts incoming connections
	 * Runs in a separate thread
	 */
	void acceptConnections();
	
	/**
	 * Handles communication with a connected client
	 * @param client_socket: Socket descriptor for the client
	 * @param client_addr: Client address information
	 * Runs in a separate thread per client
	 */
	void handleClient(int client_socket, struct sockaddr_in client_addr);
	
	/**
	 * Receives a file from a client
	 * @param client_socket: Client socket
	 * @param file_info: File information from client
	 * @param client_ip: Client IP for logging
	 * @return: true if file received successfully
	 */
	bool receiveFile(int client_socket, const FileInfo& file_info, const std::string& client_ip);
	
	/**
	 * Removes a client from the clients list
	 * @param socket_fd: Socket of client to remove
	 */
	void removeClient(int socket_fd);
	
public:
	/**
	 * Constructor - initializes server with port
	 * @param port: Port to listen on (default: 5000)
	 */
	FileTransferServer(int port = 5000);
	
	/**
	 * Destructor - ensures clean shutdown
	 */
	~FileTransferServer();
	
	/**
	 * Starts the server
	 * @return: true if server started successfully
	 */
	bool start();
	
	/**
	 * Stops the server gracefully
	 */
	void stop();
	
	/**
	 * Sets callback for when a file is completely received
	 * @param callback: Function taking (filename, filesize)
	 */
	void setFileReceivedCallback(std::function<void(const std::string&, uint64_t)> callback) {
		file_received_callback = callback;
	}
	
	/**
	 * Sets callback for transfer progress updates
	 * @param callback: Function taking (client_ip, percentage)
	 */
	void setProgressCallback(std::function<void(const std::string&, int)> callback) {
		progress_callback = callback;
	}
	
	/**
	 * Gets list of connected clients
	 * @return: Vector of client information
	 */
	std::vector<ClientInfo> getConnectedClients();
	
	/**
	 * Checks if server is running
	 * @return: Server status
	 */
	bool isRunning() const { return is_running; }
	
	/**
	 * Gets the port server is listening on
	 * @return: Port number
	 */
	int getPort() const { return port; }
	
	/**
	 * Disconnects a specific client
	 * @param client_socket: Socket of client to disconnect
	 */
	void disconnectClient(int client_socket);
	
	/**
	 * Broadcasts a message to all connected clients
	 * @param message: Message to broadcast
	 */
	void broadcastToClients(const std::string& message);
};
