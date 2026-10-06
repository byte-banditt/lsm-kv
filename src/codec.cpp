#include "lsm/codec.h"
#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

namespace lsm {
uint32_t Crc32(const uint8_t* d, size_t n) {
  uint32_t crc = ~0u;
  for (size_t i = 0; i < n; ++i) {
    crc ^= d[i];
    for (int j = 0; j < 8; ++j) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}
uint32_t Crc32(const Bytes& b) { return Crc32(b.data(), b.size()); }
void U32(Bytes& b, uint32_t x) { for (int i=0;i<4;++i) b.push_back(uint8_t(x>>(i*8))); }
void U64(Bytes& b, uint64_t x) { for (int i=0;i<8;++i) b.push_back(uint8_t(x>>(i*8))); }
uint32_t Read32(const Bytes& b, size_t& p) {
  if (p > b.size() || b.size()-p < 4) throw std::runtime_error("truncated u32");
  uint32_t x=0; for (int i=0;i<4;++i) x |= uint32_t(b[p++]) << (i*8); return x;
}
uint64_t Read64(const Bytes& b, size_t& p) {
  if (p > b.size() || b.size()-p < 8) throw std::runtime_error("truncated u64");
  uint64_t x=0; for (int i=0;i<8;++i) x |= uint64_t(b[p++]) << (i*8); return x;
}
void String(Bytes& b, std::string_view s) { b.insert(b.end(), s.begin(), s.end()); }
std::string ReadString(const Bytes& b, size_t& p, size_t n) {
  if (p > b.size() || n > b.size()-p) throw std::runtime_error("truncated string");
  std::string s(reinterpret_cast<const char*>(b.data()+p), n); p+=n; return s;
}
void WriteAll(int fd, const uint8_t* d, size_t n) {
  while (n) { ssize_t written = ::write(fd,d,n); if (written<0 && errno==EINTR) continue;
    if (written<=0) throw std::system_error(errno,std::generic_category(),"write");
    d+=written; n-=static_cast<size_t>(written); }
}
Bytes ReadFile(const std::string& path) {
  int fd=::open(path.c_str(),O_RDONLY); if (fd<0) throw std::system_error(errno,std::generic_category(),path);
  Bytes b; uint8_t buf[8192];
  try { for (;;) { ssize_t n=::read(fd,buf,sizeof buf); if(n<0 && errno==EINTR) continue;
      if(n<0) throw std::system_error(errno,std::generic_category(),"read");
      if(!n) break;
      b.insert(b.end(),buf,buf+n); } } catch (...) { ::close(fd); throw; }
  ::close(fd); return b;
}
void SyncDir(const std::string& path) {
  auto pos=path.find_last_of('/'); std::string dir=pos==std::string::npos ? "." : path.substr(0,pos);
  int fd=::open(dir.c_str(),O_RDONLY|O_DIRECTORY); if(fd<0) throw std::system_error(errno,std::generic_category(),"open directory");
  if(::fsync(fd)<0) { int e=errno; ::close(fd); throw std::system_error(e,std::generic_category(),"fsync directory"); }
  ::close(fd);
}
void AtomicWrite(const std::string& path, const Bytes& data) {
  std::string tmp=path+".tmp";
  int fd=::open(tmp.c_str(),O_CREAT|O_TRUNC|O_WRONLY,0644); if(fd<0) throw std::system_error(errno,std::generic_category(),tmp);
  try { WriteAll(fd,data.data(),data.size()); if(::fsync(fd)<0) throw std::system_error(errno,std::generic_category(),"fsync file"); }
  catch (...) { ::close(fd); throw; }
  if(::close(fd)<0) throw std::system_error(errno,std::generic_category(),"close file");
  if(::rename(tmp.c_str(),path.c_str())<0) throw std::system_error(errno,std::generic_category(),"rename");
  SyncDir(path);
}
}
