#include "lsm/db.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
using namespace lsm;
namespace {
using Clock=std::chrono::steady_clock;
constexpr int kKeys=10000;
constexpr int kReads=20000;
std::string Key(int n) { char b[17]; std::snprintf(b,sizeof b,"k%015d",n); return b; }
struct Result { double ops_s,p50_us,p99_us; };
Result Finish(std::vector<double>& us,Clock::time_point start,size_t ops) {
  double seconds=std::chrono::duration<double>(Clock::now()-start).count();
  std::sort(us.begin(),us.end());
  return {ops/seconds,us[us.size()/2],us[(us.size()*99)/100]};
}
void Print(const char* name,const Result& r) {
  std::cout<<name<<","<<r.ops_s<<","<<r.p50_us<<","<<r.p99_us<<"\n";
}
Result Fill(const std::string& dir,bool random) {
  std::filesystem::remove_all(dir); Options o; o.memtable_bytes=64*1024; o.compact_after=1000; o.sync_policy=SyncPolicy::Batched;
  DB db(dir,o); std::vector<int> order(kKeys); std::iota(order.begin(),order.end(),0);
  if(random) { std::mt19937 rng(0xabc123); std::shuffle(order.begin(),order.end(),rng); }
  std::string value(100,'v'); std::vector<double> us; us.reserve(kKeys); auto start=Clock::now();
  for(int i:order) { auto a=Clock::now(); db.Put(Key(2*i),value);
    us.push_back(std::chrono::duration<double,std::micro>(Clock::now()-a).count()); }
  db.Flush(); db.Sync(); return Finish(us,start,kKeys);
}
Result Read(const std::string& dir,bool hits,bool bloom) {
  Options o; o.memtable_bytes=64*1024; o.compact_after=1000; o.sync_policy=SyncPolicy::Batched; o.bloom_enabled=bloom;
  DB db(dir,o); std::mt19937 rng(0x51eed); std::vector<int> keys(kReads);
  for(int& k:keys) k=2*int(rng()%kKeys)+(hits?0:1);
  std::vector<double> us; us.reserve(kReads); auto start=Clock::now();
  for(int k:keys) { auto a=Clock::now(); auto v=db.Get(Key(k));
    if(v.has_value()!=hits) throw std::runtime_error("benchmark read mismatch");
    us.push_back(std::chrono::duration<double,std::micro>(Clock::now()-a).count()); }
  return Finish(us,start,kReads);
}
}
int main(int argc,char** argv) {
  if(argc!=2) { std::cerr<<"usage: lsm_bench WORKDIR\n"; return 2; }
  std::string root=argv[1]; std::filesystem::create_directories(root);
  std::cout<<"workload,ops_per_s,p50_us,p99_us\n";
  Print("sequential_fill",Fill(root+"/seq",false));
  Print("random_fill",Fill(root+"/random",true));
  Print("random_read_hit",Read(root+"/random",true,true));
  Print("random_read_miss_bloom_on",Read(root+"/random",false,true));
  Print("random_read_miss_bloom_off",Read(root+"/random",false,false));
}
