#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lsm {
using Entry = std::pair<std::string,std::optional<std::string>>;
class SkipList {
 public:
  SkipList();
  ~SkipList();
  SkipList(const SkipList&)=delete;
  SkipList& operator=(const SkipList&)=delete;
  void Put(std::string key, std::optional<std::string> value);
  bool Find(const std::string& key, std::optional<std::string>& value) const;
  std::vector<Entry> Entries() const;
  size_t BytesUsed() const { return bytes_; }
  bool Empty() const;
 private:
  struct Node { std::string key; std::optional<std::string> value; std::vector<Node*> next;
    Node(std::string k,std::optional<std::string> v,int height):key(std::move(k)),value(std::move(v)),next(height,nullptr){} };
  int Height();
  Node* head_;
  int levels_=1;
  uint32_t rng_=0x12345678u;
  size_t bytes_=0;
};
}
