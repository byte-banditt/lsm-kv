#include "lsm/db.h"
#include "lsm/codec.h"
#include "lsm/sstable.h"
#include "lsm/wal.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <random>
#include <sys/wait.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
using namespace lsm;
namespace {
struct Temp {
  std::string path;
  Temp() { char name[]="/tmp/lsm-test-XXXXXX"; char* p=::mkdtemp(name); if(!p) throw std::runtime_error("mkdtemp"); path=p; }
  ~Temp() { std::filesystem::remove_all(path); }
};
Options Small() { Options o; o.memtable_bytes=160; o.compact_after=4; return o; }
std::string Key(int n) { char b[32]; std::snprintf(b,sizeof b,"k%05d",n); return b; }
}
TEST(Codec, CrcAndBounds) {
  Bytes b; U32(b,0x12345678); U64(b,0x123456789abcdef0ull);
  size_t p=0; EXPECT_EQ(Read32(b,p),0x12345678u); EXPECT_EQ(Read64(b,p),0x123456789abcdef0ull);
  EXPECT_THROW(Read32(b,p),std::runtime_error);
  const std::string s="123456789"; EXPECT_EQ(Crc32(reinterpret_cast<const uint8_t*>(s.data()),s.size()),0xcbf43926u);
}
TEST(SkipList, OverwriteDeleteOrder) {
  SkipList m; std::optional<std::string> v;
  EXPECT_FALSE(m.Find("a",v)); m.Put("b","1"); m.Put("a","2"); m.Put("b",std::nullopt);
  ASSERT_TRUE(m.Find("b",v)); EXPECT_FALSE(v.has_value()); m.Put("b","3");
  EXPECT_EQ(m.Entries(),(std::vector<Entry>{{"a","2"},{"b","3"}}));
}
TEST(Wal, ReplayTornTailAndBatch) {
  Temp t; std::string p=t.path+"/WAL";
  { Wal w(p,SyncPolicy::Batched); w.Append({"a","1"}); w.Append({"b",std::nullopt}); w.Sync(); }
  { int fd=::open(p.c_str(),O_WRONLY|O_APPEND); ASSERT_GE(fd,0); Bytes tail={10,0,0,0,1,2}; WriteAll(fd,tail.data(),tail.size()); ::close(fd); }
  { Wal w(p,SyncPolicy::PerWrite); auto x=w.Replay(); EXPECT_EQ(x,(std::vector<Entry>{{"a","1"},{"b",std::nullopt}})); }
  { Wal w(p,SyncPolicy::PerWrite); EXPECT_EQ(w.Replay().size(),2u); }
}
TEST(Wal, CorruptTailDropsOnlyTail) {
  Temp t; std::string p=t.path+"/WAL";
  { Wal w(p,SyncPolicy::PerWrite); w.Append({"a","1"}); w.Append({"b","2"}); }
  auto bytes=ReadFile(p); bytes.back()^=0x80; AtomicWrite(p,bytes);
  { Wal w(p,SyncPolicy::PerWrite); auto e=w.Replay(); ASSERT_EQ(e.size(),1u); EXPECT_EQ(e[0],(Entry{"a","1"})); }
}
TEST(Bloom, NoFalseNegativesAndMeasuredRate) {
  constexpr int n=10000,queries=100000; Bloom b(n,10);
  for(int i=0;i<n;++i) b.Add(Key(i));
  for(int i=0;i<n;++i) ASSERT_TRUE(b.MayContain(Key(i)));
  int false_positives=0; for(int i=n;i<n+queries;++i) false_positives+=b.MayContain(Key(i));
  double actual=double(false_positives)/queries;
  double theoretical=std::pow(1-std::exp(-double(b.Hashes())*n/b.Bits()),b.Hashes());
  std::cout<<"Bloom seed=deterministic sequential keys n="<<n<<" queries="<<queries<<" actual="<<actual<<" theoretical="<<theoretical<<"\n";
  EXPECT_LT(actual,0.03);
}
TEST(SSTable, IndexBloomTombstoneAndCorruptFooter) {
  Temp t; std::string p=t.path+"/a.sst"; std::vector<Entry> entries;
  for(int i=0;i<1000;++i) entries.emplace_back(Key(i),i==500 ? std::nullopt : std::optional<std::string>("v"+std::to_string(i)));
  SSTable::Create(p,entries,10); SSTable s(p); std::optional<std::string> v;
  for(int i=0;i<1000;++i) { ASSERT_TRUE(s.Find(Key(i),v)); EXPECT_EQ(v,entries[i].second); }
  EXPECT_FALSE(s.Find("missing",v)); EXPECT_EQ(s.Entries(),entries);
  auto bytes=ReadFile(p); bytes.back()^=0xff; AtomicWrite(p,bytes);
  EXPECT_THROW(SSTable bad(p),std::runtime_error);
  bytes.resize(bytes.size()-10); AtomicWrite(p,bytes);
  EXPECT_THROW(SSTable bad(p),std::runtime_error);
}
TEST(DB, EmptyOverwriteDeleteReputScanRecovery) {
  Temp t; Options o=Small();
  { DB db(t.path,o); EXPECT_FALSE(db.Get("x")); db.Put("b","1"); db.Put("a","2"); db.Put("b","3");
    db.Delete("a"); EXPECT_FALSE(db.Get("a")); db.Put("a","4");
    EXPECT_EQ(db.Scan("a","c"),(std::vector<std::pair<std::string,std::string>>{{"a","4"},{"b","3"}})); db.Flush(); }
  { DB db(t.path,o); EXPECT_EQ(db.Get("a"),"4"); EXPECT_EQ(db.Get("b"),"3"); EXPECT_FALSE(db.Get("x")); }
}
TEST(DB, FlushRotatesWal) {
  Temp t; Options o; o.memtable_bytes=1u<<20;
  DB db(t.path,o); db.Put("a","1");
  struct stat before{},after{};
  ASSERT_EQ(::stat((t.path+"/WAL").c_str(),&before),0);
  db.Flush();
  ASSERT_EQ(::stat((t.path+"/WAL").c_str(),&after),0);
  EXPECT_NE(before.st_ino,after.st_ino);
  EXPECT_EQ(after.st_size,0);
  EXPECT_EQ(db.Get("a"),"1");
}
TEST(DB, TombstoneSurvivesWhenOlderTableExists) {
  Temp t; Options o=Small(); o.compact_after=100;
  { DB db(t.path,o); db.Put("a","old"); db.Flush(); db.Delete("a"); db.Flush(); db.Put("z","other"); db.Flush();
    EXPECT_FALSE(db.Get("a")); db.Compact(); EXPECT_FALSE(db.Get("a")); }
  { DB db(t.path,o); EXPECT_FALSE(db.Get("a")); EXPECT_EQ(db.Get("z"),"other"); }
}
TEST(DB, ManifestAndFooterCorruptionFailLoudly) {
  Temp t; { DB db(t.path,Small()); db.Put("a","1"); db.Flush(); }
  auto manifest=ReadFile(t.path+"/MANIFEST"); manifest.back()^=1; AtomicWrite(t.path+"/MANIFEST",manifest);
  EXPECT_THROW(DB db(t.path,Small()),std::runtime_error);
  manifest.back()^=1; AtomicWrite(t.path+"/MANIFEST",manifest);
  auto sst=ReadFile(t.path+"/sst-1.sst"); sst.resize(sst.size()-3); AtomicWrite(t.path+"/sst-1.sst",sst);
  EXPECT_THROW(DB db(t.path,Small()),std::runtime_error);
}
TEST(DB, RandomModel) {
  Temp t; Options o=Small(); o.memtable_bytes=2048; o.compact_after=8;
  std::mt19937 rng(0x1234abcd); std::map<std::string,std::string> model;
  {
    DB db(t.path,o);
    for(int i=0;i<10000;++i) {
      std::string k=Key(rng()%300); int action=rng()%100;
      if(action<50) { std::string v=std::to_string(rng()); db.Put(k,v); model[k]=v; }
      else if(action<75) { db.Delete(k); model.erase(k); }
      else { auto got=db.Get(k); auto it=model.find(k); EXPECT_EQ(got,it==model.end()?std::nullopt:std::optional<std::string>(it->second)); }
      if(rng()%101==0) db.Flush(); if(rng()%257==0) db.Compact();
      if(i%1000==0) { std::vector<std::pair<std::string,std::string>> expected(model.begin(),model.end()); EXPECT_EQ(db.Scan("","~"),expected); }
    }
    db.Flush(); db.Compact();
  }
  { DB db(t.path,o); for(const auto& e:model) EXPECT_EQ(db.Get(e.first),e.second);
    std::vector<std::pair<std::string,std::string>> expected(model.begin(),model.end()); EXPECT_EQ(db.Scan("","~"),expected); }
}
TEST(DB, CrashAckedWrites200Iterations) {
  constexpr uint32_t seed=0x52a71; std::mt19937 rng(seed);
  std::cout<<"Crash test seed="<<seed<<" iterations=200\n";
  for(int iteration=0;iteration<200;++iteration) {
    Temp t; int pipefd[2]; ASSERT_EQ(::pipe(pipefd),0);
    pid_t child=::fork(); ASSERT_GE(child,0);
    if(child==0) {
      ::close(pipefd[0]);
      try { Options o; o.memtable_bytes=160; o.compact_after=4; o.sync_policy=SyncPolicy::PerWrite;
        DB db(t.path,o);
        for(int i=0;i<100;++i) {
          std::string key=Key(i%5),value=std::to_string(i),ack=std::to_string(i)+"\n";
          db.Put(key,value);
          WriteAll(pipefd[1],reinterpret_cast<const uint8_t*>(ack.data()),ack.size());
          ::usleep(1000);
        }
      } catch(...) { _exit(2); }
      _exit(0);
    }
    ::close(pipefd[1]); ::usleep(1000+(rng()%20000)); ::kill(child,SIGKILL);
    int status=0; ASSERT_EQ(::waitpid(child,&status,0),child);
    std::string acks; char buf[1024]; for(;;) { ssize_t n=::read(pipefd[0],buf,sizeof buf); if(n<=0) break; acks.append(buf,n); } ::close(pipefd[0]);
    std::map<std::string,std::string> expected; size_t p=0; int last_ack=-1;
    while((p=acks.find('\n'))!=std::string::npos) {
      int i=std::stoi(acks.substr(0,p));
      ASSERT_EQ(i,last_ack+1);
      expected[Key(i%5)]=std::to_string(i); last_ack=i; acks.erase(0,p+1);
    }
    ASSERT_TRUE(acks.empty());
    auto one_in_flight=expected;
    if(last_ack+1<100) one_in_flight[Key((last_ack+1)%5)]=std::to_string(last_ack+1);
    try {
      DB recovered(t.path,Small());
      auto actual=recovered.Scan("","~");
      std::map<std::string,std::string> got(actual.begin(),actual.end());
      ASSERT_TRUE(got==expected || got==one_in_flight) << "iteration="<<iteration<<" last_ack="<<last_ack;
    }
    catch(const std::exception& e) { FAIL()<<"iteration="<<iteration<<" exception="<<e.what(); }
  }
}
