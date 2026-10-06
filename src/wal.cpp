#include "lsm/wal.h"
#include "lsm/codec.h"
#include <fcntl.h>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <unistd.h>
#include <cerrno>

namespace lsm {
namespace { constexpr uint32_t kMaxRecord=64u*1024u*1024u; }
Wal::Wal(std::string path,SyncPolicy policy):path_(std::move(path)),policy_(policy) {
  fd_=::open(path_.c_str(),O_CREAT|O_RDWR|O_APPEND,0644);
  if(fd_<0) throw std::system_error(errno,std::generic_category(),path_);
}
Wal::~Wal(){ if(fd_>=0) ::close(fd_); }
void Wal::Append(const Entry& e) {
  if(poisoned_) throw std::runtime_error("WAL unusable after I/O failure");
  if(e.first.size()>kMaxRecord-9 || (e.second && e.second->size()>kMaxRecord-9-e.first.size())) throw std::invalid_argument("WAL record too large");
  Bytes payload; payload.push_back(e.second ? 1:0); U32(payload,static_cast<uint32_t>(e.first.size()));
  U32(payload,static_cast<uint32_t>(e.second ? e.second->size():0)); String(payload,e.first);
  if(e.second) String(payload,*e.second);
  Bytes record; U32(record,static_cast<uint32_t>(payload.size())); U32(record,Crc32(payload));
  record.insert(record.end(),payload.begin(),payload.end());
  try {
    WriteAll(fd_,record.data(),record.size());
    if(policy_==SyncPolicy::PerWrite) Sync();
  } catch (...) { poisoned_=true; throw; }
}
std::vector<Entry> Wal::Replay() {
  Bytes data=ReadFile(path_); std::vector<Entry> out; size_t p=0,good=0;
  while(p<data.size()) {
    size_t start=p;
    if(data.size()-p<8) break;
    uint32_t len=Read32(data,p),crc=Read32(data,p);
    if(len<9 || len>kMaxRecord || len>data.size()-p) break;
    if(Crc32(data.data()+p,len)!=crc) break;
    size_t end=p+len;
    try { uint8_t op=data[p++]; uint32_t kn=Read32(data,p),vn=Read32(data,p);
      if(op>1 || (op==0 && vn!=0) || kn>kMaxRecord || vn>kMaxRecord || uint64_t(kn)+vn!=end-p) { p=start; break; }
      std::string key=ReadString(data,p,kn); std::optional<std::string> value;
      if(op==1) value=ReadString(data,p,vn);
      out.emplace_back(std::move(key),std::move(value));
    } catch (const std::runtime_error&) { p=start; break; }
    p=end; good=end;
  }
  if(good<data.size()) { if(::ftruncate(fd_,static_cast<off_t>(good))<0) throw std::system_error(errno,std::generic_category(),"truncate WAL tail"); Sync(); }
  return out;
}
void Wal::Sync() {
  if(poisoned_) throw std::runtime_error("WAL unusable after I/O failure");
  if(::fsync(fd_)<0) { poisoned_=true; throw std::system_error(errno,std::generic_category(),"fsync WAL"); }
}
void Wal::Rotate() {
  if(poisoned_) throw std::runtime_error("WAL unusable after I/O failure");
  std::string next=path_+".next";
  int fresh=::open(next.c_str(),O_CREAT|O_TRUNC|O_RDWR|O_APPEND,0644);
  if(fresh<0) { poisoned_=true; throw std::system_error(errno,std::generic_category(),"open next WAL"); }
  try {
    if(::fsync(fresh)<0) throw std::system_error(errno,std::generic_category(),"fsync next WAL");
    if(::rename(next.c_str(),path_.c_str())<0) throw std::system_error(errno,std::generic_category(),"rename WAL");
    SyncDir(path_);
  } catch (...) { ::close(fresh); poisoned_=true; throw; }
  ::close(fd_); fd_=fresh;
}
}
