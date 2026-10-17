#include "lsm/db.h"
#include "gate.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <numeric>
#include <fstream>
#include <regex>
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
  if(argc==4 && std::string(argv[1])=="--compare") {
    auto readMetrics=[](const char* path) {
      std::ifstream input(path);
      if(!input) throw std::runtime_error(std::string("cannot open benchmark JSON: ")+path);
      std::string json((std::istreambuf_iterator<char>(input)),{});
      auto read=[&](const char* key) {
        std::regex pattern(std::string("\\\"")+key+"\\\"\\s*:\\s*([0-9.eE+-]+)");
        std::smatch match;
        if(!std::regex_search(json,match,pattern)) throw std::runtime_error(std::string("benchmark JSON missing ")+key);
        return std::stod(match[1].str());
      };
      return lsm_bench::GateMetrics{read("put_ops_s"),read("get_ops_s"),read("get_p99_us")};
    };
    try {
      auto current=readMetrics(argv[2]);
      auto baseline=readMetrics(argv[3]);
      std::cerr<<"put_ops_s current="<<current.put_ops_s<<" baseline="<<baseline.put_ops_s<<" minimum="<<baseline.put_ops_s*0.75<<"\n"
               <<"get_ops_s current="<<current.get_ops_s<<" baseline="<<baseline.get_ops_s<<" minimum="<<baseline.get_ops_s*0.75<<"\n"
               <<"get_p99_us current="<<current.get_p99_us<<" baseline="<<baseline.get_p99_us<<" maximum="<<baseline.get_p99_us*1.25<<"\n";
      if(lsm_bench::ExceedsRegressionThreshold(current,baseline)) {
        std::cerr<<"benchmark regression over 25%\n"; return 1;
      }
      return 0;
    } catch(const std::exception& e) {
      std::cerr<<e.what()<<"\n"; return 2;
    }
  }
  if(argc!=3) { std::cerr<<"usage: lsm_bench WORKDIR RUNS | lsm_bench --compare RESULT.json BASELINE.json\n"; return 2; }
  std::string root=argv[1]; int runs=std::stoi(argv[2]);
  if(runs<1) { std::cerr<<"RUNS must be positive\n"; return 2; }
  std::filesystem::create_directories(root);
  std::vector<Result> puts,gets;
  for(int i=0;i<runs;++i) {
    std::string run=root+"/run-"+std::to_string(i);
    puts.push_back(Fill(run+"/seq",false));
    gets.push_back(Read(run+"/seq",true,true));
  }
  auto median=[](std::vector<double> values) {
    std::sort(values.begin(),values.end()); return values[values.size()/2];
  };
  auto metric=[&](const std::vector<Result>& results,double Result::*field) {
    std::vector<double> values; values.reserve(results.size());
    for(const auto& result:results) values.push_back(result.*field);
    return median(std::move(values));
  };
  Result put{metric(puts,&Result::ops_s),0,0};
  Result get{metric(gets,&Result::ops_s),metric(gets,&Result::p50_us),metric(gets,&Result::p99_us)};
  std::cout<<std::setprecision(10)
    <<"{\"put_ops_s\":"<<put.ops_s
    <<",\"get_ops_s\":"<<get.ops_s
    <<",\"get_p50_us\":"<<get.p50_us
    <<",\"get_p99_us\":"<<get.p99_us<<"}\n";
}
