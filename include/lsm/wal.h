#pragma once
#include "lsm/skiplist.h"
#include <string>
#include <vector>
namespace lsm {
enum class SyncPolicy { PerWrite, Batched };
class Wal {
 public:
  Wal(std::string path,SyncPolicy policy);
  ~Wal();
  Wal(const Wal&)=delete;
  Wal& operator=(const Wal&)=delete;
  void Append(const Entry& entry);
  std::vector<Entry> Replay();
  void Sync();
  void Rotate();
 private:
  std::string path_; SyncPolicy policy_; int fd_=-1;
  bool poisoned_=false;
};
}
