#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <nlohmann/json.hpp>

enum class MessageType {
	DISCOVERY,
	FILE_INFO,
	FILE_CHUNK,
	TRANSFER_PROGRESS,
	ERROR
};

struct FileInfo {
	std::string filename;
	uint64_t filesize;
	std::string checkHash;
};

struct TransferMessage {
	MessageType type;
	nlohmann::json data;

	std::string serialize() const;
	static TransferMessage deserialize(const std::string& jsonStr);
};
