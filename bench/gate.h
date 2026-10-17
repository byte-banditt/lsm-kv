#pragma once

namespace lsm_bench {
struct GateMetrics {
  double put_ops_s;
  double get_ops_s;
  double get_p99_us;
};

inline bool ExceedsRegressionThreshold(const GateMetrics& current,
                                       const GateMetrics& baseline) {
  return current.put_ops_s < baseline.put_ops_s * 0.75 ||
         current.get_ops_s < baseline.get_ops_s * 0.75 ||
         current.get_p99_us > baseline.get_p99_us * 1.25;
}
}
