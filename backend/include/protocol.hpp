//this header file defines the structure of the message
#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <nlohmann/json.hpp>

enum class MessageType {
	DISCOVERY,
	DISCOVERY_RESPONSE,
	FILE_INFO,
	FILE_CHUNK,
	TRANSFER_PROGRESS,
	DISCONNECT,
	ERROR
};

struct FileInfo {
	std::string filename;
	uint64_t filesize;
	std::string checksum;
};

struct TransferMessage {
	MessageType type;
	nlohmann::json data;

	std::string serialize() const;
	static TransferMessage deserialize(const std::string& jsonStr);
};
