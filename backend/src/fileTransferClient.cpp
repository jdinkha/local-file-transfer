#include "fileTransferClient.hpp"
#include <iostream>
#include <fstream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

FileTransferClient::FileTransferClient(const std::string& ip, int port) : server_ip(ip), port(port), connected(false) {

	// socket() creates an endpoint for communication
	// AF_INET: Address Family - IPv4 Internet protocols
	// SOCK_STREAM: Provides sequenced, reliable, two-way, connection-based byte streams (TCP)
	// 0: Protocol - 0 means choose default protocol for the socket type (TCP for SOCK_STREAM)
	// Why TCP? We need guaranteed delivery and ordered packets for file transfers
	client_fd = socket(AF_INET, SOCK_STREAM, 0);

	if (client_fd < 0) {
		std::cerr << "Failed to create socket. Error: " << strerror(errno) << std::endl;
	}
}

FileTransferClient::~FileTransferClient() {
	if (connected) {
		disconnect();
	}
	if (client_fd >= 0) {
		close(client_fd);
	}
}

/**
 * Establishes TCP connection to the server
 */
bool FileTransferClient::connect() {
	if (client_fd < 0) {
		std::cerr << "Invalid socket descriptor" << std::endl;
		return false;
	}

	// sockaddr_in structure for IPv4 address
	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;           // IPv4
	server_addr.sin_port = htons(port);          // Convert port to network byte order

	// Convert IP string to binary form (e.g., "192.168.1.5" -> binary)
	// inet_pton: "presentation to network" - converts text IP to binary
	if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0) {
		std::cerr << "Invalid address: " << server_ip << std::endl;
		return false;
	}

	// connect() initiates connection to the server
	// This is where the TCP three-way handshake happens
	if (::connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
		std::cerr << "Connection failed to " << server_ip << ":" << port << ". Error: " << strerror(errno) << std::endl;
		return false;
	}

	connected = true;
	std::cout << "Connected to " << server_ip << ":" << port << std::endl;
	return true;
}

/**
 * Sends a file to the server with progress tracking
 */
bool FileTransferClient::sendFile(const std::string& filepath) {
	if (!connected) {
		std::cerr << "Not connected to server" << std::endl;
		return false;
	}

	// Open file in binary mode to handle all file types correctly
	std::ifstream file(filepath, std::ios::binary | std::ios::ate);
	if (!file.is_open()) {
		std::cerr << "Cannot open file: " << filepath << std::endl;
		return false;
	}

	// Get file size (ate flag positions pointer at end)
	uint64_t file_size = file.tellg();
	file.seekg(0, std::ios::beg);  // Reset to beginning for reading

	// Extract filename from path
	size_t last_slash = filepath.find_last_of("/\\");
	std::string filename = (last_slash != std::string::npos) ? 
		filepath.substr(last_slash + 1) : filepath;

	// Create file info message using JSON for easy parsing
	TransferMessage file_info_msg;
	file_info_msg.type = MessageType::FILE_INFO;
	file_info_msg.data = {
		{"filename", filename},
		{"filesize", file_size},
		{"checksum", ""}  // TODO: We'll implement checksum later
	};

	// Send file information first
	std::string serialized = file_info_msg.serialize();
	ssize_t sent = send(client_fd, serialized.c_str(), serialized.length(), 0);
	if (sent != static_cast<ssize_t>(serialized.length())) {
		std::cerr << "Failed to send file info" << std::endl;
		return false;
	}

	// Wait for acknowledgment from server
	char ack_buffer[1024];
	ssize_t received = recv(client_fd, ack_buffer, sizeof(ack_buffer), 0);
	if (received <= 0) {
		std::cerr << "No acknowledgment from server" << std::endl;
		return false;
	}

	// Send file data in chunks to avoid loading entire file into memory
	const size_t CHUNK_SIZE = 4096;  // 4KB chunks - good balance for network
	std::vector<char> buffer(CHUNK_SIZE);
	uint64_t total_sent = 0;
	int last_percentage = -1;

	std::cout << "Starting file transfer: " << filename << " (" << file_size << " bytes)" << std::endl;

	while (file.read(buffer.data(), CHUNK_SIZE) || file.gcount() > 0) {
		size_t bytes_read = file.gcount();

		// Send chunk
		sent = send(client_fd, buffer.data(), bytes_read, 0);
		if (sent != static_cast<ssize_t>(bytes_read)) {
			std::cerr << "Failed to send file chunk" << std::endl;
			return false;
		}

		total_sent += sent;

		// Calculate and report progress
		int percentage = static_cast<int>((total_sent * 100) / file_size);
		if (percentage != last_percentage && percentage % 10 == 0) {
			std::cout << "Progress: " << percentage << "% (" 
				<< total_sent << "/" << file_size << " bytes)" << std::endl;
			last_percentage = percentage;
		}

		// Call progress callback if set
		if (progress_callback) {
			progress_callback(percentage, total_sent, file_size);
		}
	}

	std::cout << "File transfer complete: " << filename << std::endl;
	file.close();
	return true;
}

/**
 * Gracefully disconnect from server
 */
void FileTransferClient::disconnect() {
	if (connected) {
		// Send disconnect message
		TransferMessage disconnect_msg;
		disconnect_msg.type = MessageType::ERROR;  // Using ERROR type for disconnect
		disconnect_msg.data = {{"reason", "client_disconnect"}};

		std::string serialized = disconnect_msg.serialize();
		send(client_fd, serialized.c_str(), serialized.length(), 0);

		// Close the socket (this sends FIN packet for TCP termination)
		close(client_fd);
		connected = false;
		std::cout << "Disconnected from server" << std::endl;
	}
}
