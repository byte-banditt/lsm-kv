# Measured results

One run on 2026-10-06 UTC. These are measurements, not projections. Build was Release (`-O3 -DNDEBUG -std=gnu++17 -Wall -Wextra -Wpedantic`); compiler `g++ (Debian 14.2.0-19) 14.2.0`. Host reported AMD Ryzen 7 7840HS with Radeon 780M Graphics, 16 logical CPUs. WSL2 guest: Debian GNU/Linux 13 (trixie), kernel `6.18.33.2-microsoft-standard-WSL2`, 3.5 GiB reported RAM. Working tree on ext4 `/dev/sdd`; `lsblk` reports `Virtual Disk`, `ROTA=1`. Physical host disk type was not exposed to the guest.

Each fill inserts 10,000 keys, 16-byte keys and 100-byte values. Keys are even numbers from 0 to 19,998; reads perform 20,000 operations. Random fill shuffle seed `0xabc123`; random read seed `0x51eed`; misses query odd keys within the file key range except the final endpoint. Memtable threshold 64 KiB; compaction trigger 1,000 files to retain multiple tables for the read comparison. Sync policy is batched. Fill throughput includes final `Flush()` and `Sync()`; fill latency samples time each `Put`, including automatic flushes, but exclude the final explicit flush and sync. Read timing includes each `Get`. Percentiles are sorted samples at indexes `n/2` and `99n/100`. Both miss workloads reopen the same random-fill DB; only `bloom_enabled` changes. Process startup and DB construction are outside timed read loops. Single run, no confidence interval.

| Workload | Throughput (ops/s) | p50 (µs/op) | p99 (µs/op) |
|---|---:|---:|---:|
| Sequential fill | 128534 | 1.814 | 9.95 |
| Random fill | 129929 | 1.874 | 6.293 |
| Random read, hit | 28103.4 | 31.075 | 80.648 |
| Random read, miss, Bloom on | 157672 | 0.702 | 60.667 |
| Random read, miss, Bloom off | 1513.76 | 648.472 | 832.289 |

## Exact commands

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j4
ctest --test-dir build --output-on-failure
./build/lsm_bench /tmp/lsm-bench-run

cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-asan -j4
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan --output-on-failure

uname -a
cat /etc/os-release | head -6
lscpu | rg 'Model name|CPU\(s\):'
free -h
df -T . | tail -1
lsblk -d -o NAME,ROTA,TRAN,MODEL
g++ --version | head -1
cat build/CMakeFiles/lsm_kv.dir/flags.make | rg 'CXX_FLAGS|CXX_DEFINES'
./build/lsm_tests --gtest_filter=Bloom.NoFalseNegativesAndMeasuredRate:DB.CrashAckedWrites200Iterations
```

## Raw benchmark stdout

```text
workload,ops_per_s,p50_us,p99_us
sequential_fill,128534,1.814,9.95
random_fill,129929,1.874,6.293
random_read_hit,28103.4,31.075,80.648
random_read_miss_bloom_on,157672,0.702,60.667
random_read_miss_bloom_off,1513.76,648.472,832.289
```

## Test output summary

Release: `100% tests passed, 0 tests failed out of 12`; total real time `2.67 sec` on the post-rotation run. ASan+UBSan: `100% tests passed, 0 tests failed out of 12`; total real time `4.83 sec`. Focused Bloom and crash run printed `actual=0.00703 theoretical=0.00819372`, `Crash test seed=338545 iterations=200`, and `2 tests ... [ PASSED ]`.
