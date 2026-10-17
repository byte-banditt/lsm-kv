# Benchmark method and local results

## Workload

`lsm_bench WORKDIR RUNS` creates a fresh `WORKDIR/run-N/seq` database per run, then inserts 10,000 sequential even-numbered keys (`k` plus 15 decimal digits), each with a 100-byte value. It uses a 64 KiB memtable threshold, `compact_after=1000`, and batched WAL sync. Put throughput timing includes automatic flushes and the final explicit `Flush()` and `Sync()`; per-Put latency is not reported. It next reopens that database and issues 20,000 seeded random successful gets (`std::mt19937` seed `0x51eed`). Get timing includes each `Get`, but excludes open and query generation. Output is JSON with put/get ops/s and get p50/p99 microseconds, each the median across runs. Percentiles use sorted sample indexes `n/2` and `99n/100`.

## Interpreting results

The benchmark’s get phase reads 20,000 keys selected from the 10,000 keys inserted earlier; every query is a hit. `Fill()` explicitly flushes before closing its DB, and `Read()` opens a new DB, so the memtable is empty during gets. The five measured runs each contained 21 live SSTables at that point; the configured compaction threshold is 1,000. `DB::Get` probes these SSTables newest to oldest, checking key ranges and Bloom filters before reading a matching block. On open, each `SSTable` loads its whole file into the `bytes_` vector. There is no block cache or file open/read per `Get`; matching blocks are decoded from the in-memory file bytes on each lookup. Put throughput uses `Batched` WAL sync and includes flush plus final `Sync()`, while gets search the loaded SSTables, so the two ops/s figures measure different work.

## Measured results

Five Release runs on 2026-10-17 UTC; each value below is the metric median across runs. Host: WSL2, Linux `6.18.40.1-microsoft-standard-WSL2`, AMD Ryzen 7 7840HS, 16 logical CPUs. The physical storage device and host operating-system details were not measured. Values are local results, not the GitHub Actions baseline.

| Metric | Median |
|---|---:|
| Put throughput | 175094 ops/s |
| Get throughput | 33995.9 ops/s |
| Get p50 | 29.034 µs |
| Get p99 | 40.665 µs |

Bloom false-positive result from `Bloom.NoFalseNegativesAndMeasuredRate`: 10,000 inserted keys, 100,000 absent queries, 0.00703 measured, 0.00819372 theoretical. See [ADR 0003](docs/adr/0003-bloom-filter-sizing.md).

## Commands

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --target lsm_bench --parallel
./build/lsm_bench /tmp/lsm-bench-run 5

cmake -S . -B build-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests --parallel
./build-tests/lsm_tests --gtest_filter='Bloom.NoFalseNegativesAndMeasuredRate'
ctest --test-dir build-tests --output-on-failure -R '^DB\.CrashAckedWrites200Iterations$'
```

CI runs five fresh benchmark processes and compares medians with `bench/baseline.json` when present. A manual `update_baseline=true` run uploads its five-run median JSON as an artifact for review and commit.
