#pragma once
#include "lsm/skiplist.h"
#include "lsm/sstable.h"
#include "lsm/wal.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace lsm {
struct Options {
  size_t memtable_bytes=1u<<20;
  unsigned bloom_bits_per_key=10;
  bool bloom_enabled=true;
  size_t compact_after=4;
  SyncPolicy sync_policy=SyncPolicy::PerWrite;
};
class DB {
 public:
  DB(std::string directory,Options options={});
  DB(const DB&)=delete;
  DB& operator=(const DB&)=delete;
  void Put(const std::string& key,const std::string& value);
  void Delete(const std::string& key);
  std::optional<std::string> Get(const std::string& key) const;
  std::vector<std::pair<std::string,std::string>> Scan(const std::string& start,const std::string& end) const;
  void Flush();
  void Compact();
  void Sync();
  size_t TableCount() const { return tables_.size(); }
 private:
  struct Table { uint64_t id; std::unique_ptr<SSTable> file; };
  void WriteManifest(const std::vector<uint64_t>& ids,uint64_t next_id);
  std::string Path(uint64_t id) const;
  void Apply(const std::string& key,std::optional<std::string> value);
  std::string directory_;
  Options options_;
  std::unique_ptr<Wal> wal_;
  std::unique_ptr<SkipList> mem_;
  std::unique_ptr<SkipList> immutable_;
  std::vector<Table> tables_;
  uint64_t next_id_=1;
};
}
