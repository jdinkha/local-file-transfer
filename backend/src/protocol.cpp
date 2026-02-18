#include "protocol.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * Serialize a TransferMessage to JSON string for network transmission
 * Why JSON? Human-readable for debugging, and nlohmann/json is easy to use
 * For production, consider Protocol Buffers for smaller size
 */
std::string TransferMessage::serialize() const {
    json j;

    // Convert enum to string for JSON
    switch (type) {
        case MessageType::DISCOVERY:
            j["type"] = "DISCOVERY";
            break;

        case MessageType::DISCOVERY_RESPONSE:
            j["type"] = "DISCOVERY_RESPONSE";
            break;

        case MessageType::FILE_INFO:
            j["type"] = "FILE_INFO";
            break;

        case MessageType::FILE_CHUNK:
            j["type"] = "FILE_CHUNK";
            // For chunks, we need to handle binary data separately
            // This implementation assumes chunk data is base64 encoded
            j["chunk_data"] = data["chunk_data"];
            j["chunk_size"] = data["chunk_size"];
            j["chunk_index"] = data["chunk_index"];
            return j.dump();

        case MessageType::TRANSFER_PROGRESS:
            j["type"] = "TRANSFER_PROGRESS";
            j["data"] = data;
            break;

        case MessageType::DISCONNECT:
            j["type"] = "DISCONNECT";
            break;

        case MessageType::ERROR:
            j["type"] = "ERROR";
            j["data"] = data;
            break;
    }

    // For non-chunk messages, include all data
    j["data"] = data;

    return j.dump();
}

/**
 * Deserialize a JSON string back to TransferMessage
 * This reconstructs the message from network data
 */
TransferMessage TransferMessage::deserialize(const std::string& jsonStr) {
    TransferMessage msg;
    json j = json::parse(jsonStr);

    // Convert string type back to enum
    std::string type_str = j["type"];
    if (type_str == "DISCOVERY") msg.type = MessageType::DISCOVERY;
    else if (type_str == "DISCOVERY_RESPONSE") msg.type = MessageType::DISCOVERY_RESPONSE;
    else if (type_str == "FILE_INFO") msg.type = MessageType::FILE_INFO;
    else if (type_str == "FILE_CHUNK") msg.type = MessageType::FILE_CHUNK;
    else if (type_str == "TRANSFER_PROGRESS") msg.type = MessageType::TRANSFER_PROGRESS;
    else if (type_str == "DISCONNECT") msg.type = MessageType::DISCONNECT;
    else if (type_str == "ERROR") msg.type = MessageType::ERROR;

    // Handle chunk messages specially
    if (msg.type == MessageType::FILE_CHUNK) {
        msg.data["chunk_data"] = j["chunk_data"];
        msg.data["chunk_size"] = j["chunk_size"];
        msg.data["chunk_index"] = j["chunk_index"];
    }
    else {
        msg.data = j["data"];
    }

    return msg;
}

/**
 * Calculate MD5 or SHA-1 checksum of a file
 * TODO: Implement actual checksum calculation
 * This helps verify file integrity after transfer
 */
std::string calculateChecksum(const std::string& filepath) {
    // For now, return placeholder
    // In production, use OpenSSL or similar library
    return "checksum_not_implemented";
}
