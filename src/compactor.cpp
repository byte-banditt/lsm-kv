#include "lsm/compactor.h"
#include "lsm/codec.h"
#include <filesystem>
#include <queue>

namespace lsm {
namespace {
struct Cursor { std::string key; size_t file; size_t pos; };
struct LaterKey { bool operator()(const Cursor& a,const Cursor& b) const {
  if(a.key!=b.key) return a.key>b.key;
  return a.file<b.file;
} };
}

Compactor::Compactor(std::string directory,unsigned bloom_bits_per_key)
    :directory_(std::move(directory)),bloom_bits_per_key_(bloom_bits_per_key) {}

void Compactor::Compact(std::vector<Table>& tables,uint64_t& next_id,
                        const ManifestWriter& write_manifest) const {
  if(tables.size()<2) return;
  std::vector<std::vector<Entry>> inputs; inputs.reserve(tables.size());
  for(const auto& t:tables) inputs.push_back(t.file->Entries());
  std::priority_queue<Cursor,std::vector<Cursor>,LaterKey> heap;
  for(size_t i=0;i<inputs.size();++i) if(!inputs[i].empty()) heap.push({inputs[i][0].first,i,0});
  std::vector<Entry> merged;
  while(!heap.empty()) {
    Cursor c=heap.top(); heap.pop(); std::string key=c.key;
    std::optional<std::string> newest=inputs[c.file][c.pos].second;
    auto advance=[&](const Cursor& x) { size_t next=x.pos+1;
      if(next<inputs[x.file].size()) heap.push({inputs[x.file][next].first,x.file,next}); };
    advance(c);
    while(!heap.empty() && heap.top().key==key) { Cursor old=heap.top(); heap.pop(); advance(old); }
    // Every live table participates, so no older value can reappear.
    if(newest) merged.emplace_back(std::move(key),std::move(newest));
  }
  std::vector<uint64_t> old_ids; for(const auto& t:tables) old_ids.push_back(t.id);
  if(merged.empty()) {
    write_manifest({},next_id); tables.clear();
  } else {
    uint64_t id=next_id; std::string path=directory_+"/sst-"+std::to_string(id)+".sst";
    SSTable::Create(path,merged,bloom_bits_per_key_);
    auto table=std::make_unique<SSTable>(path);
    write_manifest({id},id+1);
    tables.clear(); tables.push_back({id,std::move(table)}); next_id=id+1;
  }
  for(uint64_t id:old_ids) std::filesystem::remove(directory_+"/sst-"+std::to_string(id)+".sst");
  SyncDir(directory_+"/MANIFEST");
}
}
