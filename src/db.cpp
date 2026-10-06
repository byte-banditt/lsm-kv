#include "lsm/db.h"
#include "lsm/codec.h"
#include <algorithm>
#include <filesystem>
#include <queue>
#include <stdexcept>
#include <unistd.h>

namespace lsm {
namespace {
struct Cursor { std::string key; size_t file; size_t pos; };
struct LaterKey { bool operator()(const Cursor& a,const Cursor& b) const {
  if(a.key!=b.key) return a.key>b.key;
  return a.file<b.file;
} };
}
DB::DB(std::string directory,Options options):directory_(std::move(directory)),options_(options),mem_(std::make_unique<SkipList>()) {
  if(options_.memtable_bytes==0 || options_.compact_after<2 || options_.bloom_bits_per_key>100) throw std::invalid_argument("invalid DB options");
  std::filesystem::create_directories(directory_);
  std::string manifest=directory_+"/MANIFEST";
  if(std::filesystem::exists(manifest)) {
    Bytes b=ReadFile(manifest); if(b.size()<24) throw std::runtime_error("manifest truncated");
    size_t p=0; if(ReadString(b,p,8)!="LSMMAN01") throw std::runtime_error("manifest magic");
    next_id_=Read64(b,p); uint32_t count=Read32(b,p);
    if(count>(b.size()-24)/8 || b.size()!=24ull+8ull*count) throw std::runtime_error("manifest length");
    uint64_t previous=0;
    for(uint32_t i=0;i<count;++i) { uint64_t id=Read64(b,p);
      if(id<=previous || id>=next_id_) throw std::runtime_error("manifest ID order");
      tables_.push_back({id,std::make_unique<SSTable>(Path(id))}); previous=id; }
    uint32_t crc=Read32(b,p); if(Crc32(b.data(),b.size()-4)!=crc) throw std::runtime_error("manifest checksum");
  }
  bool new_wal=!std::filesystem::exists(directory_+"/WAL");
  wal_=std::make_unique<Wal>(directory_+"/WAL",options_.sync_policy);
  if(new_wal) SyncDir(directory_+"/WAL");
  for(auto& e:wal_->Replay()) mem_->Put(std::move(e.first),std::move(e.second));
}
std::string DB::Path(uint64_t id) const { return directory_+"/sst-"+std::to_string(id)+".sst"; }
void DB::WriteManifest(const std::vector<uint64_t>& ids,uint64_t next_id) {
  Bytes b; String(b,"LSMMAN01"); U64(b,next_id); U32(b,static_cast<uint32_t>(ids.size()));
  for(uint64_t id:ids) U64(b,id);
  U32(b,Crc32(b)); AtomicWrite(directory_+"/MANIFEST",b);
}
void DB::Apply(const std::string& key,std::optional<std::string> value) {
  wal_->Append({key,value}); mem_->Put(key,std::move(value));
  if(mem_->BytesUsed()>=options_.memtable_bytes) Flush();
}
void DB::Put(const std::string& key,const std::string& value) { Apply(key,value); }
void DB::Delete(const std::string& key) { Apply(key,std::nullopt); }
std::optional<std::string> DB::Get(const std::string& key) const {
  std::optional<std::string> value;
  if(mem_->Find(key,value)) return value;
  if(immutable_ && immutable_->Find(key,value)) return value;
  for(auto it=tables_.rbegin();it!=tables_.rend();++it)
    if(it->file->Find(key,value,options_.bloom_enabled)) return value;
  return std::nullopt;
}
std::vector<std::pair<std::string,std::string>> DB::Scan(const std::string& start,const std::string& end) const {
  std::map<std::string,std::optional<std::string>> merged;
  for(const auto& table:tables_) for(auto& e:table.file->Entries()) if(e.first>=start && e.first<end) merged[e.first]=std::move(e.second);
  if(immutable_) for(auto& e:immutable_->Entries()) if(e.first>=start && e.first<end) merged[e.first]=std::move(e.second);
  for(auto& e:mem_->Entries()) if(e.first>=start && e.first<end) merged[e.first]=std::move(e.second);
  std::vector<std::pair<std::string,std::string>> result;
  for(auto& e:merged) if(e.second) result.emplace_back(e.first,*e.second);
  return result;
}
void DB::Flush() {
  if(mem_->Empty()) return;
  immutable_=std::move(mem_); mem_=std::make_unique<SkipList>();
  uint64_t id=next_id_; std::string path=Path(id);
  try {
    SSTable::Create(path,immutable_->Entries(),options_.bloom_bits_per_key);
    auto table=std::make_unique<SSTable>(path);
    std::vector<uint64_t> ids; for(const auto& x:tables_) ids.push_back(x.id); ids.push_back(id);
    WriteManifest(ids,id+1);
    tables_.push_back({id,std::move(table)}); next_id_=id+1;
    wal_->Reset(); immutable_.reset();
  } catch (...) {
    if(!mem_->Empty()) throw std::logic_error("unexpected concurrent write during flush");
    mem_=std::move(immutable_); throw;
  }
  if(tables_.size()>=options_.compact_after) Compact();
}
void DB::Compact() {
  if(tables_.size()<2) return;
  std::vector<std::vector<Entry>> inputs; inputs.reserve(tables_.size());
  for(const auto& t:tables_) inputs.push_back(t.file->Entries());
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
  std::vector<uint64_t> old_ids; for(const auto& t:tables_) old_ids.push_back(t.id);
  if(merged.empty()) {
    WriteManifest({},next_id_); tables_.clear();
  } else {
    uint64_t id=next_id_; std::string path=Path(id);
    SSTable::Create(path,merged,options_.bloom_bits_per_key);
    auto table=std::make_unique<SSTable>(path);
    WriteManifest({id},id+1);
    tables_.clear(); tables_.push_back({id,std::move(table)}); next_id_=id+1;
  }
  for(uint64_t id:old_ids) std::filesystem::remove(Path(id));
  SyncDir(directory_+"/MANIFEST");
}
void DB::Sync() { wal_->Sync(); }
}
