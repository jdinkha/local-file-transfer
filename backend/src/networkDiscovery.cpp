#include "networkDiscovery.hpp"
#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/if.h>       // For network interfaces
#include <ifaddrs.h>      // For getting network interface addresses
#include <unistd.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace std::chrono_literals;

NetworkDiscovery::NetworkDiscovery() 
    : discovery_socket(-1), listen_socket(-1), is_listening(false) {}

NetworkDiscovery::~NetworkDiscovery() {
    stopListening();
    if (discovery_socket >= 0) close(discovery_socket);
    if (listen_socket >= 0 && listen_socket != discovery_socket) close(listen_socket);
}

bool NetworkDiscovery::initialize(int listen_port) {
    // Create UDP socket for broadcasting
    // SOCK_DGRAM: UDP - we don't need reliability for discovery
    // UDP allows broadcast, TCP doesn't support broadcasting
    discovery_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (discovery_socket < 0) {
        std::cerr << "Failed to create discovery socket: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Enable broadcast option
    // SO_BROADCAST: Allows sending to broadcast addresses
    int broadcast_enable = 1;
    if (setsockopt(discovery_socket, SOL_SOCKET, SO_BROADCAST, 
                   &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        std::cerr << "Failed to set broadcast option: " << strerror(errno) << std::endl;
        close(discovery_socket);
        return false;
    }
    
    // Create listening socket (separate for simplicity)
    listen_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (listen_socket < 0) {
        std::cerr << "Failed to create listen socket: " << strerror(errno) << std::endl;
        close(discovery_socket);
        return false;
    }
    
    // Bind listening socket to receive broadcast responses
    struct sockaddr_in listen_addr;
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = INADDR_ANY;  // Listen on all interfaces
    listen_addr.sin_port = htons(listen_port);
    
    // SO_REUSEADDR: Allow reuse of address even if it's in TIME_WAIT state
    int reuse = 1;
    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    if (bind(listen_socket, (struct sockaddr*)&listen_addr, sizeof(listen_addr)) < 0) {
        std::cerr << "Failed to bind listen socket: " << strerror(errno) << std::endl;
        close(discovery_socket);
        close(listen_socket);
        return false;
    }
    
    return true;
}

void NetworkDiscovery::broadcastDiscovery(int broadcast_port) {
    if (discovery_socket < 0) {
        std::cerr << "Discovery socket not initialized" << std::endl;
        return;
    }
    
    // Prepare discovery message
    json discovery_msg = {
        {"type", "DISCOVERY"},
        {"service", "FILE_TRANSFER"},
        {"version", "1.0"}
    };
    std::string message = discovery_msg.dump();
    
    // Broadcast to network
    struct sockaddr_in broadcast_addr;
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(broadcast_port);
    broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST;  // 255.255.255.255
    
    // Get list of network interfaces to broadcast on each
    struct ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces) == 0) {
        for (struct ifaddrs* ifa = interfaces; ifa != nullptr; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
                // Skip loopback interface (127.0.0.1)
                struct sockaddr_in* addr = (struct sockaddr_in*)ifa->ifa_addr;
                if (ntohl(addr->sin_addr.s_addr) == 0x7f000001) {  // 127.0.0.1
                    continue;
                }
                
                // Calculate broadcast address for this interface
                struct sockaddr_in* netmask = (struct sockaddr_in*)ifa->ifa_netmask;
                uint32_t ip = ntohl(addr->sin_addr.s_addr);
                uint32_t mask = ntohl(netmask->sin_addr.s_addr);
                uint32_t broadcast = ip | ~mask;
                
                broadcast_addr.sin_addr.s_addr = htonl(broadcast);
                
                // Send discovery message
                sendto(discovery_socket, message.c_str(), message.length(), 0,
                      (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr));
                
                char ip_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &broadcast_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
                std::cout << "Broadcast discovery on " << ip_str << std::endl;
            }
        }
        freeifaddrs(interfaces);
    }
}

void NetworkDiscovery::startListening() {
    if (is_listening) return;
    
    is_listening = true;
    
    // Start listener thread
    std::thread([this]() {
        char buffer[1024];
        struct sockaddr_in sender_addr;
        socklen_t sender_len = sizeof(sender_addr);
        
        // Set timeout for recvfrom (1 second)
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        setsockopt(listen_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        
        while (is_listening) {
            ssize_t received = recvfrom(listen_socket, buffer, sizeof(buffer) - 1, 0,
                                       (struct sockaddr*)&sender_addr, &sender_len);
            
            if (received > 0) {
                buffer[received] = '\0';
                
                try {
                    // Parse response
                    json response = json::parse(std::string(buffer));
                    
                    if (response["type"] == "DISCOVERY_RESPONSE" && 
                        response["service"] == "FILE_TRANSFER") {
                        
                        DiscoveredDevice device;
                        
                        // Convert sender IP to string
                        char ip_str[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &(sender_addr.sin_addr), ip_str, INET_ADDRSTRLEN);
                        device.ip_address = ip_str;
                        
                        device.port = response.value("port", 5000);  // Default to 5000
                        device.device_name = response.value("name", "Unknown Device");
                        
                        // Simple response time calculation (using sequence numbers would be better)
                        device.response_time = 0;
                        
                        // Add to discovered devices (avoid duplicates)
                        bool found = false;
                        for (const auto& existing : discovered_devices) {
                            if (existing.ip_address == device.ip_address) {
                                found = true;
                                break;
                            }
                        }
                        
                        if (!found) {
                            discovered_devices.push_back(device);
                            std::cout << "Discovered device: " << device.device_name 
                                     << " at " << device.ip_address << std::endl;
                            
                            if (device_found_callback) {
                                device_found_callback(device);
                            }
                        }
                    }
                } catch (const json::exception& e) {
                    // Not JSON or invalid format, ignore
                }
            }
        }
    }).detach();  // Detach so it runs independently
}

void NetworkDiscovery::stopListening() {
    is_listening = false;
    // Small delay to allow thread to exit
    std::this_thread::sleep_for(100ms);
}
