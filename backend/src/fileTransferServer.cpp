#include "fileTransferServer.hpp"
#include <iostream>
#include <fstream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <iomanip>

using json = nlohmann::json;
using namespace std::chrono_literals;

/**
 * Constructor - initializes server with port
 * Creates the server socket but doesn't start listening yet
 */
FileTransferServer::FileTransferServer(int port) 
	: port(port), is_running(false), server_fd(-1), accept_thread(nullptr) {
	
	// Create server socket
	// AF_INET: IPv4
	// SOCK_STREAM: TCP (reliable, connection-oriented)
	// 0: Default protocol (TCP for SOCK_STREAM)
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	
	if (server_fd < 0) {
		std::cerr << "Failed to create server socket. Error: " << strerror(errno) << std::endl;
		return;
	}
	
	// Set socket option to reuse address
	// SO_REUSEADDR: Allows binding to a port that's in TIME_WAIT state
	// This prevents "Address already in use" errors when restarting the server
	int opt = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		std::cerr << "Failed to set socket options: " << strerror(errno) << std::endl;
		close(server_fd);
		server_fd = -1;
		return;
	}
	
	std::cout << "Server socket created successfully" << std::endl;
}

/**
 * Destructor - ensures clean shutdown
 */
FileTransferServer::~FileTransferServer() {
	stop();
	if (server_fd >= 0) {
		close(server_fd);
	}
}

/**
 * Starts the server - binds to port and begins listening for connections
 */
bool FileTransferServer::start() {
	if (server_fd < 0) {
		std::cerr << "Invalid server socket" << std::endl;
		return false;
	}
	
	// Configure server address structure
	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;  // Listen on all network interfaces
	server_addr.sin_port = htons(port);         // Convert port to network byte order
	
	// Bind socket to port
	// bind() associates the socket with the specified address and port
	if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
		std::cerr << "Failed to bind to port " << port << ": " << strerror(errno) << std::endl;
		return false;
	}
	
	// Start listening for connections
	// listen() marks the socket as passive (waiting for connections)
	// 5 is the maximum length of the pending connections queue
	if (listen(server_fd, 5) < 0) {
		std::cerr << "Failed to listen on socket: " << strerror(errno) << std::endl;
		return false;
	}
	
	is_running = true;
	std::cout << "Server listening on port " << port << std::endl;
	
	// Start the accept thread to handle incoming connections
	accept_thread = new std::thread(&FileTransferServer::acceptConnections, this);
	
	return true;
}

/**
 * Stops the server gracefully
 * Closes all client connections and stops the accept thread
 */
void FileTransferServer::stop() {
	if (!is_running) return;
	
	std::cout << "Shutting down server..." << std::endl;
	is_running = false;
	
	// Close all client connections
	{
		std::lock_guard<std::mutex> lock(clients_mutex);
		for (auto& client : clients) {
			if (client.socket_fd >= 0) {
				close(client.socket_fd);
			}
			if (client.handler_thread && client.handler_thread->joinable()) {
				client.handler_thread->join();
				delete client.handler_thread;
			}
		}
		clients.clear();
	}
	
	// Close server socket to interrupt accept()
	if (server_fd >= 0) {
		close(server_fd);
		server_fd = -1;
	}
	
	// Wait for accept thread to finish
	if (accept_thread && accept_thread->joinable()) {
		accept_thread->join();
		delete accept_thread;
		accept_thread = nullptr;
	}
	
	std::cout << "Server shutdown complete" << std::endl;
}

/**
 * Main accept loop - runs in separate thread
 * Continuously accepts new client connections
 */
void FileTransferServer::acceptConnections() {
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);
	
	while (is_running) {
		// accept() blocks until a client connects
		// Returns a new socket descriptor for the client connection
		int client_socket = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
		
		if (!is_running) {
			break;  // Server is shutting down
		}
		
		if (client_socket < 0) {
			std::cerr << "Failed to accept connection: " << strerror(errno) << std::endl;
			continue;
		}
		
		// Convert client IP to string
		char client_ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
		int client_port = ntohs(client_addr.sin_port);
		
		std::cout << "New connection from " << client_ip << ":" << client_port << std::endl;
		
		// Create a new thread to handle this client
		std::thread* client_thread = new std::thread(&FileTransferServer::handleClient, 
													 this, client_socket, client_addr);
		
		// Store client info
		ClientInfo info;
		info.socket_fd = client_socket;
		info.ip_address = client_ip;
		info.port = client_port;
		info.handler_thread = client_thread;
		info.is_active = true;
		info.bytes_received = 0;
		
		{
			std::lock_guard<std::mutex> lock(clients_mutex);
			clients.push_back(info);
		}
	}
}

/**
 * Handles communication with a connected client
 * Runs in a separate thread for each client
 */
void FileTransferServer::handleClient(int client_socket, struct sockaddr_in client_addr) {
	char client_ip[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
	
	char buffer[4096];
	
	while (is_running) {
		// Receive message from client
		// recv() blocks until data is received or connection closes
		ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
		
		if (bytes_received <= 0) {
			// Connection closed or error
			if (bytes_received == 0) {
				std::cout << "Client " << client_ip << " closed connection" << std::endl;
			} else {
				std::cerr << "Error receiving from client " << client_ip << ": " 
						  << strerror(errno) << std::endl;
			}
			break;
		}
		
		buffer[bytes_received] = '\0';
		
		try {
			// Parse the message
			std::string message_str(buffer, bytes_received);
			TransferMessage msg = TransferMessage::deserialize(message_str);
			
			switch (msg.type) {
				case MessageType::FILE_INFO: {
					// Client is about to send a file
					FileInfo file_info;
					file_info.filename = msg.data["filename"];
					file_info.filesize = msg.data["filesize"];
					
					std::cout << "Receiving file: " << file_info.filename 
							 << " (" << file_info.filesize << " bytes)" << std::endl;
					
					// Update client info with filename
					{
						std::lock_guard<std::mutex> lock(clients_mutex);
						for (auto& client : clients) {
							if (client.socket_fd == client_socket) {
								client.current_filename = file_info.filename;
								client.bytes_received = 0;
								break;
							}
						}
					}
					
					// Send acknowledgment
					json ack = {{"status", "ready"}};
					send(client_socket, ack.dump().c_str(), ack.dump().length(), 0);
					
					// Receive the actual file
					receiveFile(client_socket, file_info, client_ip);
					break;
				}
				
				case MessageType::DISCONNECT: {
					std::cout << "Client " << client_ip << " sent disconnect" << std::endl;
					removeClient(client_socket);
					return;
				}
				
				case MessageType::ERROR: {
					std::string error_msg = msg.data.value("reason", "Unknown error");
					std::cerr << "Error from client " << client_ip << ": " << error_msg << std::endl;
					break;
				}
				
				default:
					std::cout << "Received message type " << static_cast<int>(msg.type) 
							 << " from " << client_ip << std::endl;
					break;
			}
		} catch (const std::exception& e) {
			std::cerr << "Error processing message from " << client_ip << ": " << e.what() << std::endl;
		}
	}
	
	// Client thread ending - clean up
	removeClient(client_socket);
}

/**
 * Receives a file from a client
 */
bool FileTransferServer::receiveFile(int client_socket, const FileInfo& file_info, const std::string& client_ip) {
	// Create filename with timestamp to avoid overwrites
	auto now = std::chrono::system_clock::now();
	auto time = std::chrono::system_clock::to_time_t(now);
	std::stringstream timestamp;
	timestamp << std::put_time(std::localtime(&time), "_%Y%m%d_%H%M%S");
	
	// Add received_ prefix and timestamp to avoid collisions
	std::string output_filename = "received_" + file_info.filename;
	size_t dot_pos = output_filename.find_last_of('.');
	if (dot_pos != std::string::npos) {
		// Insert timestamp before extension
		output_filename = output_filename.substr(0, dot_pos) + timestamp.str() + 
						 output_filename.substr(dot_pos);
	} else {
		// No extension, just append timestamp
		output_filename += timestamp.str();
	}
	
	// Open file for writing in binary mode
	std::ofstream output_file(output_filename, std::ios::binary);
	if (!output_file.is_open()) {
		std::cerr << "Failed to create output file: " << output_filename << std::endl;
		json error = {{"status", "error"}, {"reason", "Cannot create file"}};
		send(client_socket, error.dump().c_str(), error.dump().length(), 0);
		return false;
	}
	
	std::cout << "Creating file: " << output_filename << std::endl;
	
	// Send ready signal
	json ready = {{"status", "receiving"}};
	send(client_socket, ready.dump().c_str(), ready.dump().length(), 0);
	
	// Receive file data
	const size_t BUFFER_SIZE = 8192;
	char buffer[BUFFER_SIZE];
	uint64_t total_received = 0;
	int last_percentage = -1;
	
	while (total_received < file_info.filesize) {
		// Calculate remaining bytes to receive
		uint64_t remaining = file_info.filesize - total_received;
		size_t to_receive = (remaining < BUFFER_SIZE) ? remaining : BUFFER_SIZE;
		
		ssize_t received = recv(client_socket, buffer, to_receive, 0);
		if (received <= 0) {
			std::cerr << "Error receiving file data: " << strerror(errno) << std::endl;
			output_file.close();
			// Delete partial file
			std::remove(output_filename.c_str());
			return false;
		}
		
		// Write to file
		output_file.write(buffer, received);
		total_received += received;
		
		// Update client info
		{
			std::lock_guard<std::mutex> lock(clients_mutex);
			for (auto& client : clients) {
				if (client.socket_fd == client_socket) {
					client.bytes_received = total_received;
					break;
				}
			}
		}
		
		// Calculate and report progress
		int percentage = static_cast<int>((total_received * 100) / file_info.filesize);
		if (percentage != last_percentage && percentage % 10 == 0) {
			std::cout << "Receiving from " << client_ip << ": " << percentage << "% "
					 << "(" << total_received << "/" << file_info.filesize << " bytes)" << std::endl;
			last_percentage = percentage;
			
			// Call progress callback if set
			if (progress_callback) {
				progress_callback(client_ip, percentage);
			}
		}
	}
	
	output_file.close();
	std::cout << "File received successfully: " << output_filename 
			 << " (" << total_received << " bytes)" << std::endl;
	
	// Call file received callback
	if (file_received_callback) {
		file_received_callback(output_filename, total_received);
	}
	
	// Send completion message
	json complete = {{"status", "complete"}, {"filename", output_filename}};
	send(client_socket, complete.dump().c_str(), complete.dump().length(), 0);
	
	return true;
}

/**
 * Removes a client from the clients list
 */
void FileTransferServer::removeClient(int socket_fd) {
	std::lock_guard<std::mutex> lock(clients_mutex);
	
	auto it = std::find_if(clients.begin(), clients.end(), 
		[socket_fd](const ClientInfo& client) { return client.socket_fd == socket_fd; });
	
	if (it != clients.end()) {
		std::cout << "Removing client " << it->ip_address << ":" << it->port << std::endl;
		
		// Close socket if still open
		if (it->socket_fd >= 0) {
			close(it->socket_fd);
		}
		
		// Join and delete thread
		if (it->handler_thread && it->handler_thread->joinable()) {
			it->handler_thread->join();
			delete it->handler_thread;
		}
		
		clients.erase(it);
	}
}

/**
 * Gets list of connected clients
 */
std::vector<ClientInfo> FileTransferServer::getConnectedClients() {
	std::lock_guard<std::mutex> lock(clients_mutex);
	return clients;
}

/**
 * Disconnects a specific client
 */
void FileTransferServer::disconnectClient(int client_socket) {
	// Send disconnect message to client
	TransferMessage disconnect_msg;
	disconnect_msg.type = MessageType::DISCONNECT;
	disconnect_msg.data = {{"reason", "server_shutdown"}};
	
	std::string serialized = disconnect_msg.serialize();
	send(client_socket, serialized.c_str(), serialized.length(), 0);
	
	// Remove client from list (this will also close the socket)
	removeClient(client_socket);
}

/**
 * Broadcasts a message to all connected clients
 */
void FileTransferServer::broadcastToClients(const std::string& message) {
	std::lock_guard<std::mutex> lock(clients_mutex);
	
	for (const auto& client : clients) {
		if (client.is_active && client.socket_fd >= 0) {
			ssize_t sent = send(client.socket_fd, message.c_str(), message.length(), 0);
			if (sent != static_cast<ssize_t>(message.length())) {
				std::cerr << "Failed to broadcast to client " << client.ip_address << std::endl;
			}
		}
	}
}
