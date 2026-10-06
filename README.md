# lsm-kv

Small, single-process C++17 LSM key-value store for Linux. No storage engine dependency. GoogleTest is fetched by CMake for tests.

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j4
ctest --test-dir build --output-on-failure
./build/lsm_bench /tmp/lsm-bench-run
```

Public API: `lsm::DB db(path, options)`, `Put(key, value)`, `Get(key)`, `Delete(key)`, `Scan(start, end)`, `Flush()`, `Compact()`, `Sync()`. `Get` returns `std::optional<std::string>`. `Scan` returns sorted live pairs in `[start, end)`. Keys and values may be empty and may contain binary bytes. `SyncPolicy::PerWrite` acknowledges only after WAL `fsync`; `Batched` calls `fsync` at explicit `Sync()` or WAL rotation. A successful batched `Put` alone has no crash-durability guarantee.

## Architecture

```text
Put/Delete --> WAL append + policy sync --> mutable skip list
                                            |
                                    threshold reached
                                            v
                                     immutable skip list
                                            |
                              sorted blocks + index + Bloom
                                            v
                                      new SSTable
                                            |
                              atomic MANIFEST replacement
                                            |
                                  atomic WAL rotation

Get: mutable --> immutable --> SSTables newest to oldest
       tombstone stops search; key range and Bloom skip files
```

Skip list uses 12 possible heights and a deterministic xorshift height generator. Its approximate byte count triggers synchronous flush. Flush swaps in an empty mutable skip list, writes the old one to an SSTable, commits a new manifest, rotates the WAL, then releases the immutable list. No background thread or concurrent caller is supported.

## Disk formats

All integers are little endian. Strings are raw bytes with lengths; CRC32 uses polynomial `0xEDB88320`.

- **WAL:** repeated `[payload length:u32][payload CRC32:u32][payload]`. Payload is `[op:u8][key length:u32][value length:u32][key][value]`; op `1` means value, `0` means tombstone. A record is capped at 64 MiB. Replay stops at the first invalid or incomplete record, truncates that suffix, and syncs the truncation.
- **SSTable:** sorted data blocks, sparse index, Bloom filter, 56-byte footer. Each block is `[payload length:u32][payload CRC32:u32][count:u32][records...]`; records have the WAL payload layout. Index is `[count:u32]` followed by `[first key length:u32][first key][block offset:u64]` per block. Bloom region is `[bit count:u32][hash count:u32][bitset]`. Footer is `["LSMKV001":8][data end:u64][index offset:u64][index length:u64][Bloom offset:u64][Bloom length:u64][preceding-file CRC32:u32][footer CRC32:u32]`. Open verifies the whole-file checksum, footer checksum and offsets, every block checksum, index alignment, and sorted unique keys. An invalid referenced SSTable makes open fail.
- **MANIFEST:** `["LSMMAN01":8][next id:u64][table count:u32][ascending live table ids:u64...][CRC32:u32]`. Replace via temporary file, file `fsync`, rename, directory `fsync`. SSTables and WAL use the same rename and directory sync pattern when installed. Orphan SSTables and `WAL.next` files from interrupted operations are ignored.

## Recovery and compaction

Open verifies MANIFEST and all referenced SSTables, then replays WAL into the mutable skip list. A crash before MANIFEST replacement leaves the old WAL to restore writes; a crash after replacement may replay duplicate records, which have the same values. WAL replacement happens only after MANIFEST is durable. Tail corruption in WAL is discarded; corrupted referenced SSTables or MANIFEST fail loudly.

Compaction uses a **single-tier, count-triggered size-tiered strategy**: at `compact_after` files (default 4), merge all live SSTables into one sorted SSTable. A priority queue merges per-file sorted iterators; for equal keys, the newest table ID wins. Tombstones can be dropped because every older live SSTable participates. A tombstone stays in an SSTable until such a full merge. The new table and MANIFEST become durable before old files are removed. This simple policy bounds table probes between merges but rewrites all live data at each trigger; transient disk use includes both old and new tables. It is a deliberately small size-tiered variant, without size classes or levels. `Scan` materializes and merges all entries in memory.

Bloom filter uses double hashing (FNV-1a plus mixed odd step); bits per key is configurable. In the executed deterministic test, 10,000 inserted keys and 100,000 absent queries gave **0.00703** false-positive rate; formula `(1-exp(-kn/m))^k` gave **0.00819372** for the configured bit and hash counts. Bloom has no false negatives in that test.

## Verified tests

`ctest` passed **12/12** in Release and **12/12** under AddressSanitizer plus UBSan. Included: unit tests for codec, skip list, WAL, Bloom, SSTable, DB; corrupt/truncated WAL tail; corrupt/truncated SST footer; manifest corruption; 10,000-operation randomized `std::map` model; and 200 SIGKILL/reopen iterations with repeated-key child acknowledgments sent only after `Put` returns (recovery may also include one later in-flight write). Crash timing seed: **338545** (`0x52a71`). See [BENCHMARKS.md](BENCHMARKS.md) for exact commands and measured performance.

## Known limits

Single process, single caller; no locking, snapshots, transactions, or concurrent reads during flush. `Scan` and compaction may use RAM proportional to all table entries; SST files are fully cached in RAM on open, then block lookup uses the sparse index. No cache eviction. Checksums detect corruption at open, but cannot reconstruct damaged acknowledged data. WAL replay treats the first bad record as the tail; arbitrary corruption in the middle may discard later records. Durability depends on filesystem and host honoring `fsync` and rename; WSL2/physical power-loss behavior is not proven. No compression, multi-level compaction, size-class grouping, or portable on-disk format migration. Caller must not continue writing after an I/O error; WAL blocks later appends after its I/O failure. Batched writes can be lost before `Sync()`.
