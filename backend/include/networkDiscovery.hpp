#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

/**
 * Structure to hold discovered device information
 */
struct DiscoveredDevice {
    std::string ip_address;
    std::string device_name;  // Optional: could be hostname
    int port;                  // Port where file transfer service is running
    uint64_t response_time;    // For sorting by latency
};

/**
 * NetworkDiscovery class handles finding other devices on the local network
 * Uses UDP broadcast for discovery (UDP because we don't need reliable delivery
 * for discovery messages - we just send and hope someone responds)
 */
class NetworkDiscovery {
private:
    int discovery_socket;      // UDP socket for broadcasting
    int listen_socket;         // UDP socket for listening to responses
    bool is_listening;
    std::vector<DiscoveredDevice> discovered_devices;

    // Callback for when new devices are found
    std::function<void(const DiscoveredDevice&)> device_found_callback;

public:
    NetworkDiscovery();
    ~NetworkDiscovery();

    /**
     * Initialize discovery sockets
     * @param listen_port: Port to listen for responses (default: 8888)
     * @return: true if successful
     */
    bool initialize(int listen_port = 8888);

    /**
     * Broadcast discovery message to find other devices
     * @param broadcast_port: Port to broadcast to (default: 8888)
     */
    void broadcastDiscovery(int broadcast_port = 8888);

    /**
     * Start listening for discovery responses (non-blocking)
     * This will run in a separate thread
     */
    void startListening();

    /**
     * Stop listening for responses
     */
    void stopListening();

    /**
     * Get list of discovered devices
     */
    const std::vector<DiscoveredDevice>& getDiscoveredDevices() const {
	return discovered_devices;
    }

    /**
     * Clear discovered devices list (for fresh discovery)
     */
    void clearDiscoveredDevices() {
	discovered_devices.clear();
    }

    /**
     * Set callback for device discovery
     */
    void setDeviceFoundCallback(std::function<void(const DiscoveredDevice&)> callback) {
	device_found_callback = callback;
    }
};
