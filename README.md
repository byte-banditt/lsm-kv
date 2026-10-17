# lsm-kv

[![CI](https://github.com/byte-banditt/lsm-kv/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/byte-banditt/lsm-kv/actions/workflows/ci.yml?query=branch%3Amaster)

lsm-kv is a small, single-process C++17 key-value store for Linux that writes updates to a write-ahead log and a skip-list memtable, flushes sorted tables to disk, and merges tables with size-tiered compaction.

## Architecture

- `Wal` persists each write before acknowledgment in `PerWrite` mode; `Batched` mode syncs only on explicit `Sync()` or WAL rotation.
- `SkipList` is the mutable memtable. When its approximate byte count reaches the configured limit, `DB` swaps it for an immutable memtable and flushes it to an `SSTable`.
- `SSTable` stores sorted blocks, a sparse index, and a Bloom filter. `DB::Get` checks mutable memory, immutable memory, then tables newest to oldest; tombstones stop the search.
- `Compactor` merges all live SSTables when the table count reaches `compact_after` (default 4). New table and MANIFEST become durable before old files are removed.
- On open, `DB` validates MANIFEST and referenced SSTables, then replays WAL into the mutable memtable. Replay drops and truncates the first invalid or incomplete WAL suffix.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Durability guarantees

With the default `PerWrite` policy, a successful `DB::Put` or `DB::Delete` has its WAL record fsynced before the call returns. This supports recovery after a process crash while the OS remains running. `DB.CrashAckedWrites200Iterations` tests SIGKILL/reopen recovery under this policy; it does not simulate OS failure or power loss. `Batched` mode does not fsync each WAL append, so an acknowledged write is not individually guaranteed durable until `DB::Sync()` or a flush has installed its SSTable and MANIFEST.

For OS crash or power loss, the code fsyncs WAL records in `PerWrite` mode, and fsyncs file contents plus the containing directory when atomically installing WAL, SSTable, and MANIFEST files. This requests durability from the OS and storage stack, but cannot guarantee that a filesystem or device honors fsync during power loss. `Batched` writes not yet synced or installed by a flush may be lost. No test simulates OS crash or power loss; the SIGKILL test backs only the process-crash claim.

Run the 200-iteration SIGKILL/reopen recovery test alone:

```sh
ctest --test-dir build --output-on-failure -R '^DB\.CrashAckedWrites200Iterations$'
```

## Benchmark

The deterministic workload inserts 10,000 sequential keys with 100-byte values, then performs 20,000 seeded random successful gets. Output is one JSON object. For the CI five-run median and gate, run the `bench` workflow. Manual `workflow_dispatch` with `update_baseline=true` uploads a five-run median artifact; commit it as `bench/baseline.json` to enable the gate.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --target lsm_bench --parallel
./build/lsm_bench /tmp/lsm-bench-run 5
```

## Results

Five local benchmark runs, median per metric. Release build on WSL2, Linux 6.18.40.1, AMD Ryzen 7 7840HS, 16 logical CPUs. These are local measurements, not CI baseline values.

| Metric | Median |
|---|---:|
| Put throughput | 175094 ops/s |
| Get throughput | 33995.9 ops/s |
| Get p50 | 29.034 µs |
| Get p99 | 40.665 µs |
| Bloom false-positive rate | 0.00703 measured; 0.00819372 theoretical |

The Bloom measurement used 10,000 inserted sequential keys and 100,000 absent sequential queries, from the existing deterministic test.

## Architecture decisions

- [WAL fsync policy](docs/adr/0001-wal-fsync-policy.md)
- [Compaction strategy](docs/adr/0002-compaction-strategy.md)
- [Bloom filter sizing](docs/adr/0003-bloom-filter-sizing.md)

The store supports one process and one caller. It has no locking, snapshots, or transactions. `Scan` and compaction use memory proportional to table contents; opened SSTables are fully cached in RAM. See [BENCHMARKS.md](BENCHMARKS.md) for workload details.
