#pragma once
#include "lsm/codec.h"
#include "lsm/skiplist.h"
#include <optional>
#include <string>
#include <vector>
namespace lsm {
class Bloom {
 public:
  Bloom()=default;
  Bloom(size_t keys,unsigned bits_per_key);
  void Add(const std::string& key);
  bool MayContain(const std::string& key) const;
  Bytes Encode() const;
  static Bloom Decode(const Bytes& data,size_t begin,size_t end);
  size_t Bits() const { return bits_; }
  unsigned Hashes() const { return hashes_; }
 private:
  size_t bits_=0; unsigned hashes_=0; Bytes data_;
};
class SSTable {
 public:
  static void Create(const std::string& path,const std::vector<Entry>& entries,unsigned bits_per_key);
  explicit SSTable(std::string path);
  bool Find(const std::string& key,std::optional<std::string>& value,bool bloom_enabled=true) const;
  std::vector<Entry> Entries() const;
  const std::string& First() const { return first_; }
  const std::string& Last() const { return last_; }
 private:
  std::vector<Entry> Block(size_t off) const;
  std::string path_,first_,last_;
  Bytes bytes_;
  Bloom bloom_;
  std::vector<std::pair<std::string,size_t>> index_;
  size_t data_end_=0;
};
}
