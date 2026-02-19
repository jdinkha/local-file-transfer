#include "fileTransferServer.hpp"
#include <iostream>
#include <fstream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <chrono>
#include <iomanip>
#include <atomic>  // For atomic flags

using json = nlohmann::json;
using namespace std::chrono_literals;

/**
 * Constructor - initializes server with port
 */
FileTransferServer::FileTransferServer(int port) : port(port), is_running(false), server_fd(-1), accept_thread(nullptr) {

	server_fd = socket(AF_INET, SOCK_STREAM, 0);

	if (server_fd < 0) {
		std::cerr << "Failed to create server socket. Error: " << strerror(errno) << std::endl;
		return;
	}

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
		server_fd = -1;
	}
}

/**
 * Starts the server
 */
bool FileTransferServer::start() {
	if (server_fd < 0) {
		std::cerr << "Invalid server socket" << std::endl;
		return false;
	}

	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(port);

	if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
		std::cerr << "Failed to bind to port " << port << ": " << strerror(errno) << std::endl;
		return false;
	}

	if (listen(server_fd, 5) < 0) {
		std::cerr << "Failed to listen on socket: " << strerror(errno) << std::endl;
		return false;
	}

	is_running = true;
	std::cout << "Server listening on port " << port << std::endl;

	// Start accept thread
	accept_thread = new std::thread(&FileTransferServer::acceptConnections, this);

	return true;
}

/**
 * Stops the server gracefully
 */
void FileTransferServer::stop() {
	if (!is_running) return;

	std::cout << "\nShutting down server..." << std::endl;
	is_running = false;

	// Close server socket to interrupt accept()
	if (server_fd >= 0) {
		shutdown(server_fd, SHUT_RDWR);  // SHUT_RDWR: Stop both reading and writing
		close(server_fd);
		server_fd = -1;
	}

	// Close all client connections
	{
		std::lock_guard<std::mutex> lock(clients_mutex);
		for (auto& client : clients) {
			if (client.socket_fd >= 0) {
				shutdown(client.socket_fd, SHUT_RDWR);
				close(client.socket_fd);
				client.socket_fd = -1;
			}
			// Don't join threads here - they need to exit naturally
			client.is_active = false;
		}
	}

	// Wait for accept thread with timeout
	if (accept_thread && accept_thread->joinable()) {
		// Give the thread time to finish (2 seconds)
		auto start = std::chrono::steady_clock::now();
		while (std::chrono::steady_clock::now() - start < 2s) {
			if (accept_thread->joinable()) {
				accept_thread->join();
				break;
			}
			std::this_thread::sleep_for(100ms);
		}

		// If thread still running, detach it
		if (accept_thread->joinable()) {
			accept_thread->detach();
		}
		delete accept_thread;
		accept_thread = nullptr;
	}

	// Wait for client threads to finish (with timeout)
	auto start = std::chrono::steady_clock::now();
	while (std::chrono::steady_clock::now() - start < 3s) {
		bool all_done = true;
		{
			std::lock_guard<std::mutex> lock(clients_mutex);
			for (auto& client : clients) {
				if (client.handler_thread && client.handler_thread->joinable()) {
					all_done = false;
					break;
				}
			}
		}

		if (all_done) {
			break;
		}
		std::this_thread::sleep_for(100ms);
	}

	// Clean up any remaining threads
	std::lock_guard<std::mutex> lock(clients_mutex);
	for (auto& client : clients) {
		if (client.handler_thread) {
			if (client.handler_thread->joinable()) {
				client.handler_thread->detach();  // Detach instead of join to avoid deadlock
			}
			delete client.handler_thread;
		}
	}
	clients.clear();

	std::cout << "Server shutdown complete" << std::endl;
}

/**
 * Main accept loop
 */
void FileTransferServer::acceptConnections() {
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);

	// Set socket timeout for accept() to allow checking is_running
	struct timeval timeout;
	timeout.tv_sec = 1;  // 1 second timeout
	timeout.tv_usec = 0;
	setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

	while (is_running) {
		// accept() will now timeout after 1 second if no connection
		int client_socket = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);

		if (!is_running) {
			if (client_socket >= 0) close(client_socket);
			break;
		}

		if (client_socket < 0) {
			if (errno != EAGAIN && errno != EWOULDBLOCK) {
				std::cerr << "Failed to accept connection: " << strerror(errno) << std::endl;
			}
			continue;
		}

		// Disable Nagle's algorithm for better performance with small packets
		int flag = 1;
		setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

		char client_ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
		int client_port = ntohs(client_addr.sin_port);

		std::cout << "New connection from " << client_ip << ":" << client_port << std::endl;

		// Create client thread
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
 * Handles client communication
 */
void FileTransferServer::handleClient(int client_socket, struct sockaddr_in client_addr) {
	char client_ip[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);

	char buffer[4096];

	// Set socket timeout for recv()
	struct timeval timeout;
	timeout.tv_sec = 5;  // 5 second timeout
	timeout.tv_usec = 0;
	setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

	while (is_running) {
		ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);

		if (bytes_received < 0) {
			// Check if it's a timeout or real error
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				// Timeout, just continue if still running
				continue;
			}
			// Real error
			if (errno != ECONNRESET) {  // Don't log connection reset as error
				std::cerr << "Error receiving from client " << client_ip << ": " 
					<< strerror(errno) << std::endl;
			}
			break;
		}

		if (bytes_received == 0) {
			// Connection closed by client
			std::cout << "Client " << client_ip << " closed connection gracefully" << std::endl;
			break;
		}

		buffer[bytes_received] = '\0';

		try {
			std::string message_str(buffer, bytes_received);
			TransferMessage msg = TransferMessage::deserialize(message_str);

			switch (msg.type) {
				case MessageType::FILE_INFO: {
					FileInfo file_info;
					file_info.filename = msg.data["filename"];
					file_info.filesize = msg.data["filesize"];

					std::cout << "Receiving file: " << file_info.filename 
						<< " (" << file_info.filesize << " bytes)" << std::endl;

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
					std::string ack_str = ack.dump();
					send(client_socket, ack_str.c_str(), ack_str.length(), 0);

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
					if (error_msg != "client_disconnect" && error_msg != "client_finished") {
						std::cerr << "Error from client " << client_ip << ": " << error_msg << std::endl;
					}
					break;
				}

				default:
					std::cout << "Received message type " << static_cast<int>(msg.type) 
						<< " from " << client_ip << std::endl;
					break;
			}
		} catch (const json::exception& e) {
			std::cerr << "JSON parse error from " << client_ip << ": " << e.what() << std::endl;
		} catch (const std::exception& e) {
			std::cerr << "Error processing message from " << client_ip << ": " << e.what() << std::endl;
		}
	}

	// Clean up
	removeClient(client_socket);
}

/**
 * Receives a file from a client
 */
bool FileTransferServer::receiveFile(int client_socket, const FileInfo& file_info, const std::string& client_ip) {
	// Create filename
	auto now = std::chrono::system_clock::now();
	auto time = std::chrono::system_clock::to_time_t(now);

	std::string output_filename = file_info.filename;

	// Open file
	std::ofstream output_file(output_filename, std::ios::binary);
	if (!output_file.is_open()) {
		std::cerr << "Failed to create output file: " << output_filename << std::endl;
		json error = {{"status", "error"}, {"reason", "Cannot create file"}};
		std::string error_str = error.dump();
		send(client_socket, error_str.c_str(), error_str.length(), 0);
		return false;
	}

	std::cout << "Creating file: " << output_filename << std::endl;

	// Send ready signal
	json ready = {{"status", "receiving"}};
	std::string ready_str = ready.dump();
	send(client_socket, ready_str.c_str(), ready_str.length(), 0);

	// Receive file data
	const size_t BUFFER_SIZE = 8192;
	char buffer[BUFFER_SIZE];
	uint64_t total_received = 0;
	int last_percentage = -1;

	while (is_running && total_received < file_info.filesize) {
		uint64_t remaining = file_info.filesize - total_received;
		size_t to_receive = (remaining < BUFFER_SIZE) ? remaining : BUFFER_SIZE;

		ssize_t received = recv(client_socket, buffer, to_receive, 0);

		if (received < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				continue;  // Timeout, try again
			}
			std::cerr << "Error receiving file data: " << strerror(errno) << std::endl;
			output_file.close();
			std::remove(output_filename.c_str());
			return false;
		}

		if (received == 0) {
			std::cerr << "Connection closed during file transfer" << std::endl;
			output_file.close();
			std::remove(output_filename.c_str());
			return false;
		}

		output_file.write(buffer, received);
		total_received += received;

		{
			std::lock_guard<std::mutex> lock(clients_mutex);
			for (auto& client : clients) {
				if (client.socket_fd == client_socket) {
					client.bytes_received = total_received;
					break;
				}
			}
		}

		int percentage = static_cast<int>((total_received * 100) / file_info.filesize);
		if (percentage != last_percentage && percentage % 10 == 0) {
			std::cout << "Receiving from " << client_ip << ": " << percentage << "% "
				<< "(" << total_received << "/" << file_info.filesize << " bytes)" << std::endl;
			last_percentage = percentage;

			if (progress_callback) {
				progress_callback(client_ip, percentage);
			}
		}
	}

	output_file.close();

	if (total_received == file_info.filesize) {
		std::cout << "File received successfully: " << output_filename 
			<< " (" << total_received << " bytes)" << std::endl;

		if (file_received_callback) {
			file_received_callback(output_filename, total_received);
		}

		json complete = {{"status", "complete"}, {"filename", output_filename}};
		std::string complete_str = complete.dump();
		send(client_socket, complete_str.c_str(), complete_str.length(), 0);

		return true;
	} else {
		std::cerr << "File transfer incomplete: received " << total_received 
			<< " of " << file_info.filesize << " bytes" << std::endl;
		std::remove(output_filename.c_str());
		return false;
	}
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

		// Mark as inactive first
		it->is_active = false;

		// Close socket to interrupt any blocking recv/send
		if (it->socket_fd >= 0) {
			shutdown(it->socket_fd, SHUT_RDWR);
			close(it->socket_fd);
			it->socket_fd = -1;
		}

		// DETACH the thread instead of joining - let it clean itself up
		// The thread function will exit when it detects is_active=false or socket closed
		if (it->handler_thread && it->handler_thread->joinable()) {
			it->handler_thread->detach();  // Detach, don't join!
		}

		// Delete the thread object (safe after detach)
		delete it->handler_thread;

		// Remove from vector
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
	TransferMessage disconnect_msg;
	disconnect_msg.type = MessageType::DISCONNECT;
	disconnect_msg.data = {{"reason", "server_shutdown"}};

	std::string serialized = disconnect_msg.serialize();
	send(client_socket, serialized.c_str(), serialized.length(), 0);

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
