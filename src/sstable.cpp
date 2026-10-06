#include "lsm/sstable.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace lsm {
namespace {
uint64_t Hash(const std::string& s) {
  uint64_t h=14695981039346656037ull;
  for(unsigned char c:s) { h^=c; h*=1099511628211ull; }
  return h;
}
uint64_t Mix(uint64_t h) {
  h^=h>>33; h*=0xff51afd7ed558ccdull; h^=h>>33;
  h*=0xc4ceb9fe1a85ec53ull; return h^(h>>33);
}
void Record(Bytes& out,const Entry& e) {
  if(e.first.size()>std::numeric_limits<uint32_t>::max() || (e.second && e.second->size()>std::numeric_limits<uint32_t>::max())) throw std::invalid_argument("SST record too large");
  out.push_back(e.second ? 1:0); U32(out,static_cast<uint32_t>(e.first.size()));
  U32(out,static_cast<uint32_t>(e.second ? e.second->size():0)); String(out,e.first);
  if(e.second) String(out,*e.second);
}
constexpr size_t kFooter=56;
}
Bloom::Bloom(size_t keys,unsigned bits_per_key) {
  if(bits_per_key==0) return;
  if(keys>std::numeric_limits<uint32_t>::max()/bits_per_key) throw std::invalid_argument("Bloom too large");
  bits_=std::max<size_t>(64,keys*bits_per_key); bits_=(bits_+7)&~size_t(7);
  hashes_=std::max(1u,std::min(30u,static_cast<unsigned>(std::lround(bits_per_key*0.6931471805599453))));
  data_.resize(bits_/8);
}
void Bloom::Add(const std::string& key) {
  if(!bits_) return;
  uint64_t h=Hash(key),delta=Mix(h)|1ull;
  for(unsigned i=0;i<hashes_;++i) { size_t bit=(h+i*delta)%bits_; data_[bit/8]|=uint8_t(1u<<(bit%8)); }
}
bool Bloom::MayContain(const std::string& key) const {
  if(!bits_) return true;
  uint64_t h=Hash(key),delta=Mix(h)|1ull;
  for(unsigned i=0;i<hashes_;++i) { size_t bit=(h+i*delta)%bits_; if(!(data_[bit/8]&(1u<<(bit%8)))) return false; }
  return true;
}
Bytes Bloom::Encode() const { Bytes b; U32(b,static_cast<uint32_t>(bits_)); U32(b,hashes_); b.insert(b.end(),data_.begin(),data_.end()); return b; }
Bloom Bloom::Decode(const Bytes& b,size_t begin,size_t end) {
  if(begin>end || end>b.size() || end-begin<8) throw std::runtime_error("bad Bloom region");
  size_t p=begin; Bloom f; f.bits_=Read32(b,p); f.hashes_=Read32(b,p);
  if((f.bits_==0)!=(f.hashes_==0) || f.hashes_>30 || f.bits_%8 || f.bits_/8!=end-p) throw std::runtime_error("bad Bloom metadata");
  f.data_.assign(b.begin()+static_cast<ptrdiff_t>(p),b.begin()+static_cast<ptrdiff_t>(end)); return f;
}
void SSTable::Create(const std::string& path,const std::vector<Entry>& entries,unsigned bits_per_key) {
  if(entries.empty()) throw std::invalid_argument("empty SST");
  for(size_t i=1;i<entries.size();++i) if(!(entries[i-1].first<entries[i].first)) throw std::invalid_argument("SST keys not unique sorted");
  Bytes out; std::vector<std::pair<std::string,size_t>> index; Bloom bloom(entries.size(),bits_per_key);
  size_t i=0;
  while(i<entries.size()) {
    size_t off=out.size(); index.emplace_back(entries[i].first,off); Bytes payload; U32(payload,0);
    size_t count=0;
    do { Record(payload,entries[i]); bloom.Add(entries[i].first); ++i; ++count; }
    while(i<entries.size() && payload.size()<4096);
    for(int j=0;j<4;++j) payload[j]=uint8_t(count>>(j*8));
    U32(out,static_cast<uint32_t>(payload.size())); U32(out,Crc32(payload)); out.insert(out.end(),payload.begin(),payload.end());
  }
  uint64_t data_end=out.size(),index_off=out.size();
  U32(out,static_cast<uint32_t>(index.size()));
  for(const auto& x:index) { U32(out,static_cast<uint32_t>(x.first.size())); String(out,x.first); U64(out,x.second); }
  uint64_t index_len=out.size()-index_off,bloom_off=out.size(); Bytes bf=bloom.Encode();
  out.insert(out.end(),bf.begin(),bf.end()); uint64_t bloom_len=bf.size();
  uint32_t checksum=Crc32(out);
  String(out,"LSMKV001"); U64(out,data_end); U64(out,index_off); U64(out,index_len);
  U64(out,bloom_off); U64(out,bloom_len); U32(out,checksum);
  uint32_t footer_crc=Crc32(out.data()+out.size()-(kFooter-4),kFooter-4); U32(out,footer_crc);
  AtomicWrite(path,out);
}
SSTable::SSTable(std::string path):path_(std::move(path)),bytes_(ReadFile(path_)) {
  if(bytes_.size()<kFooter) throw std::runtime_error("SST footer truncated: "+path_);
  size_t p=bytes_.size()-kFooter,footer_start=p;
  if(ReadString(bytes_,p,8)!="LSMKV001") throw std::runtime_error("SST footer magic: "+path_);
  data_end_=Read64(bytes_,p); uint64_t index_off=Read64(bytes_,p),index_len=Read64(bytes_,p);
  uint64_t bloom_off=Read64(bytes_,p),bloom_len=Read64(bytes_,p); uint32_t checksum=Read32(bytes_,p),footer_crc=Read32(bytes_,p);
  if(Crc32(bytes_.data()+footer_start,kFooter-4)!=footer_crc || data_end_!=index_off || index_off>footer_start || index_len>footer_start-index_off || bloom_off!=index_off+index_len || bloom_len!=footer_start-bloom_off || Crc32(bytes_.data(),footer_start)!=checksum)
    throw std::runtime_error("SST checksum/offset error: "+path_);
  p=index_off; size_t end=bloom_off; uint32_t count=Read32(bytes_,p);
  if(count==0 || count>data_end_/12+1) throw std::runtime_error("bad SST index count");
  for(uint32_t i=0;i<count;++i) {
    uint32_t n=Read32(bytes_,p); std::string key=ReadString(bytes_,p,n); uint64_t off=Read64(bytes_,p);
    if(off>=data_end_ || (i==0 && off!=0) || (!index_.empty() && (key<=index_.back().first || off<=index_.back().second))) throw std::runtime_error("bad SST index");
    index_.emplace_back(std::move(key),off);
  }
  if(p!=end) throw std::runtime_error("SST index trailing bytes");
  bloom_=Bloom::Decode(bytes_,bloom_off,bloom_off+bloom_len);
  std::string prev; bool have=false; size_t pos=0,block=0;
  while(pos<data_end_) {
    if(block>=index_.size() || pos!=index_[block].second) throw std::runtime_error("SST block/index mismatch");
    auto entries=Block(pos); if(entries.empty() || entries.front().first!=index_[block].first) throw std::runtime_error("SST first key mismatch");
    for(const auto& e:entries) { if(have && e.first<=prev) throw std::runtime_error("SST keys out of order");
      if(!have) first_=e.first;
      prev=e.first; have=true; last_=e.first; }
    size_t q=pos; uint32_t len=Read32(bytes_,q); pos+=8+len; ++block;
  }
  if(pos!=data_end_ || block!=index_.size()) throw std::runtime_error("SST data length mismatch");
}
std::vector<Entry> SSTable::Block(size_t off) const {
  size_t p=off; if(off>data_end_ || data_end_-off<8) throw std::runtime_error("SST block truncated");
  uint32_t len=Read32(bytes_,p),crc=Read32(bytes_,p); if(len<4 || len>data_end_-p || Crc32(bytes_.data()+p,len)!=crc) throw std::runtime_error("SST block corrupt");
  size_t end=p+len; uint32_t count=Read32(bytes_,p); if(count>(end-p)/9) throw std::runtime_error("SST bad record count");
  std::vector<Entry> entries; entries.reserve(count);
  for(uint32_t i=0;i<count;++i) { if(p>=end) throw std::runtime_error("SST record truncated"); uint8_t op=bytes_[p++];
    uint32_t kn=Read32(bytes_,p),vn=Read32(bytes_,p);
    if(op>1 || (op==0 && vn) || uint64_t(kn)+vn>end-p) throw std::runtime_error("SST record invalid");
    std::string key=ReadString(bytes_,p,kn); std::optional<std::string> val;
    if(op) val=ReadString(bytes_,p,vn);
    entries.emplace_back(std::move(key),std::move(val)); }
  if(p!=end) throw std::runtime_error("SST block trailing bytes");
  return entries;
}
bool SSTable::Find(const std::string& key,std::optional<std::string>& value,bool bloom_enabled) const {
  if(key<first_ || key>last_ || (bloom_enabled && !bloom_.MayContain(key))) return false;
  auto it=std::upper_bound(index_.begin(),index_.end(),key,[](const std::string& k,const auto& x){return k<x.first;});
  if(it==index_.begin()) return false;
  --it;
  auto entries=Block(it->second); auto found=std::lower_bound(entries.begin(),entries.end(),key,[](const Entry& x,const std::string& k){return x.first<k;});
  if(found==entries.end() || found->first!=key) return false;
  value=found->second; return true;
}
std::vector<Entry> SSTable::Entries() const { std::vector<Entry> out;
  for(const auto& x:index_) { auto block=Block(x.second); out.insert(out.end(),std::make_move_iterator(block.begin()),std::make_move_iterator(block.end())); }
  return out;
}
}
