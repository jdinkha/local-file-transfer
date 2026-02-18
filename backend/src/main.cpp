#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include "fileTransferServer.hpp"
#include "networkDiscovery.hpp"
#include "fileTransferClient.hpp"
//#include "protocol.hpp"

// Global pointers for signal handling
FileTransferServer* g_server = nullptr;
NetworkDiscovery* g_discovery = nullptr;

/**
 * Signal handler for graceful shutdown
 * When user presses Ctrl+C, we need to clean up properly
 */
void signalHandler(int signum) {
    std::cout << "\nInterrupt signal (" << signum << ") received. Shutting down..." << std::endl;
    
    if (g_server) {
        g_server->stop();
    }
    
    if (g_discovery) {
        g_discovery->stopListening();
    }
    
    exit(signum);
}

/**
 * Main function - entry point of the application
 * Shows a menu for either server or client mode
 */
int main() {
    // Register signal handler for Ctrl+C
    signal(SIGINT, signalHandler);
    
    std::cout << "=== File Transfer Application ===" << std::endl;
    std::cout << "1. Start Server (Receive files)" << std::endl;
    std::cout << "2. Start Client (Send files)" << std::endl;
    std::cout << "3. Discover devices" << std::endl;
    std::cout << "Choice: ";
    
    int choice;
    std::cin >> choice;
    
    if (choice == 1) {
        // Server mode
        FileTransferServer server(5000);
        g_server = &server;
        
        if (server.start()) {
            std::cout << "Server running. Press Ctrl+C to stop." << std::endl;
            
            // Keep main thread alive
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        } else {
            std::cerr << "Failed to start server" << std::endl;
        }
    }
    else if (choice == 2) {
        // Client mode
        std::string server_ip;
        std::string filepath;
        
        std::cout << "Enter server IP: ";
        std::cin >> server_ip;
        
        std::cout << "Enter file path: ";
        std::cin >> filepath;
        
        FileTransferClient client(server_ip, 5000);
        
        // Set progress callback
        client.setProgressCallback([](int percentage, uint64_t sent, uint64_t total) {
            // Progress updates will be handled here
        });
        
        if (client.connect()) {
            client.sendFile(filepath);
            client.disconnect();
        }
    }
    else if (choice == 3) {
        // Discovery mode
        NetworkDiscovery discovery;
        g_discovery = &discovery;
        
        if (discovery.initialize()) {
            std::cout << "Discovering devices... (Press Ctrl+C to stop)" << std::endl;
            
            // Set callback for found devices
            discovery.setDeviceFoundCallback([](const DiscoveredDevice& device) {
                std::cout << "Found device: " << device.device_name 
                         << " (" << device.ip_address << ":" << device.port << ")" << std::endl;
            });
            
            discovery.startListening();
            
            // Broadcast discovery every 5 seconds
            while (true) {
                discovery.broadcastDiscovery();
                std::this_thread::sleep_for(std::chrono::seconds(5));
                discovery.clearDiscoveredDevices();  // Refresh list
            }
        }
    }
    
    return 0;
}
