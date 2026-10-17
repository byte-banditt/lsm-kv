#pragma once
#include "lsm/sstable.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lsm {
class Compactor {
 public:
  struct Table { uint64_t id; std::unique_ptr<SSTable> file; };
  using ManifestWriter = std::function<void(const std::vector<uint64_t>&, uint64_t)>;

  Compactor(std::string directory, unsigned bloom_bits_per_key);
  void Compact(std::vector<Table>& tables, uint64_t& next_id,
               const ManifestWriter& write_manifest) const;

 private:
  std::string directory_;
  unsigned bloom_bits_per_key_;
};
}
