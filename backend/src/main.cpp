#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include "fileTransferServer.hpp"
#include "fileTransferClient.hpp"
#include "networkDiscovery.hpp"

// Global pointers for signal handling
FileTransferServer* g_server = nullptr;
NetworkDiscovery* g_discovery = nullptr;
std::atomic<bool> g_running{true};

/**
 * Signal handler for graceful shutdown
 */
void signalHandler(int signum) {
    static bool already_shutting_down = false;
    
    if (already_shutting_down) {
        std::cout << "\nForced shutdown..." << std::endl;
        exit(signum);
    }
    
    already_shutting_down = true;
    std::cout << "\nInterrupt signal (" << signum << ") received. Shutting down..." << std::endl;
    g_running = false;
}

/**
 * Main function
 */
int main() {
    // Register signal handler for Ctrl+C
    signal(SIGINT, signalHandler);
    
    while (g_running) {
        std::cout << "\n=== File Transfer Application ===" << std::endl;
        std::cout << "1. Start Server (Receive files)" << std::endl;
        std::cout << "2. Start Client (Send files)" << std::endl;
        std::cout << "3. Discover devices" << std::endl;
        std::cout << "4. Exit" << std::endl;
        std::cout << "Choice: ";
        
        int choice;
        std::cin >> choice;
        
        if (!g_running) break;
        
        if (choice == 1) {
            // Server mode
            FileTransferServer server(5000);
            g_server = &server;
            
            if (server.start()) {
                std::cout << "Server running. Press Ctrl+C to stop." << std::endl;
                
                // Keep main thread alive
                while (g_running) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                
                server.stop();
            } else {
                std::cerr << "Failed to start server" << std::endl;
            }
            g_server = nullptr;
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
            
            client.setProgressCallback([](int percentage, uint64_t sent, uint64_t total) {
                std::cout << "\rProgress: " << percentage << "% (" << sent << "/" << total << " bytes)" << std::flush;
                if (percentage == 100) std::cout << std::endl;
            });
            
            if (client.connect()) {
                client.sendFile(filepath);
                client.disconnect();
            }
            
            std::cout << "\nPress Enter to continue...";
            std::cin.ignore();
            std::cin.get();
        }
        else if (choice == 3) {
            // Discovery mode
            NetworkDiscovery discovery;
            g_discovery = &discovery;
            
            if (discovery.initialize()) {
                std::cout << "Discovering devices... (Press Ctrl+C to stop)" << std::endl;
                
                discovery.setDeviceFoundCallback([](const DiscoveredDevice& device) {
                    std::cout << "Found device: " << device.device_name 
                             << " (" << device.ip_address << ":" << device.port << ")" << std::endl;
                });
                
                discovery.startListening();
                
                // Broadcast discovery every 5 seconds
                while (g_running) {
                    discovery.broadcastDiscovery();
                    
                    // Wait with interruption checking
                    for (int i = 0; i < 50 && g_running; i++) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    
                    if (g_running) {
                        discovery.clearDiscoveredDevices();
                    }
                }
                
                discovery.stopListening();
            }
            g_discovery = nullptr;
        }
        else if (choice == 4) {
            break;
        }
    }
    
    std::cout << "Application terminated." << std::endl;
    return 0;
}
