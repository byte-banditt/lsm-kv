#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lsm {
using Bytes = std::vector<uint8_t>;
uint32_t Crc32(const uint8_t* data, size_t size);
uint32_t Crc32(const Bytes& b);
void U32(Bytes& b, uint32_t x);
void U64(Bytes& b, uint64_t x);
uint32_t Read32(const Bytes& b, size_t& p);
uint64_t Read64(const Bytes& b, size_t& p);
void String(Bytes& b, std::string_view s);
std::string ReadString(const Bytes& b, size_t& p, size_t n);
void WriteAll(int fd, const uint8_t* data, size_t n);
Bytes ReadFile(const std::string& path);
void SyncDir(const std::string& path);
void AtomicWrite(const std::string& path, const Bytes& data);
}
