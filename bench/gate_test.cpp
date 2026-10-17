#include "gate.h"
#include <iostream>

int main() {
  const lsm_bench::GateMetrics baseline{100.0,100.0,100.0};
  const lsm_bench::GateMetrics equal{100.0,100.0,100.0};
  const lsm_bench::GateMetrics worse{66.0,66.0,200.0};
  const lsm_bench::GateMetrics better{200.0,200.0,50.0};
  const lsm_bench::GateMetrics boundary{75.0,75.0,125.0};
  if(lsm_bench::ExceedsRegressionThreshold(equal,baseline)) {
    std::cerr<<"equal baseline should pass\n"; return 1;
  }
  if(!lsm_bench::ExceedsRegressionThreshold(worse,baseline)) {
    std::cerr<<"worse throughput and latency should fail\n"; return 1;
  }
  if(lsm_bench::ExceedsRegressionThreshold(better,baseline)) {
    std::cerr<<"better throughput and latency should pass\n"; return 1;
  }
  if(lsm_bench::ExceedsRegressionThreshold(boundary,baseline)) {
    std::cerr<<"exact 25% boundaries should pass\n"; return 1;
  }
  std::cout<<"benchmark gate: equal, worse, better, exact boundary passed\n";
}
