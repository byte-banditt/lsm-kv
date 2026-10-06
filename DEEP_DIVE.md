# Interview deep dive

1. **Why append WAL before changing the skip list?** `DB::Apply` calls `Wal::Append` first. With `PerWrite`, it waits for `fsync` before the new value becomes visible in memory. A crash cannot leave an acknowledged memory-only write under the stated `fsync` assumptions.

2. **What does `fsync` guarantee here?** Linux reports file contents and needed metadata synchronized through its filesystem stack. It does not prove that WSL2's host or physical device survives power loss. New names also need directory `fsync`; `AtomicWrite` and `Wal::Rotate` do it.

3. **Why sync the directory after rename?** File `fsync` covers file data, not necessarily the parent directory entry that points to it. `SyncDir` follows the rename so a committed MANIFEST or WAL name survives a filesystem crash where supported.

4. **What happens if a WAL append tears?** `Wal::Replay` checks length, maximum size, CRC, and payload structure. At first invalid record it truncates from that offset and syncs the shortened WAL. Valid preceding records replay. It cannot infer whether corruption occurred in the middle.

5. **How does flush avoid losing data?** `DB::Flush` swaps the mutable skip list to `immutable_`, writes an SSTable durably, replaces MANIFEST durably, then rotates WAL. Before MANIFEST commit, old WAL recovers entries. After it, the SSTable is authoritative, even if old WAL replays duplicates.

6. **Why keep an immutable memtable?** It represents the flush input while a new mutable skip list exists. `Get` checks it between mutable skip list and SSTables. Current implementation is synchronous and supports one caller, so the overlap is a clear flush state, not a concurrency feature.

7. **Why a skip list?** Ordered traversal gives `Flush` already-sorted entries; expected lookup and insert are logarithmic. This code owns nodes and forward pointers itself, with 12 possible heights and xorshift-generated heights. A fixed seed makes structure deterministic in tests.

8. **How are SST block and footer tears detected?** Each data block has CRC32, the footer has CRC32, and the footer records a checksum of every preceding byte. `SSTable` validates offsets, block/index alignment, and sorted keys at open. A corrupt referenced file raises an exception.

9. **Why sparse index?** It stores only first key and offset per block. `Find` binary-searches those keys, then searches one decoded block. The implementation still caches whole SST bytes in RAM; index reduces parsing, not resident memory.

10. **What is Bloom math here?** For `m` bits, `n` keys, `k` hashes, approximate false-positive rate is `(1-exp(-kn/m))^k`. Constructor chooses roughly `k=(m/n)ln 2`, capped at 30. The executed test observed 0.00703 over 100,000 misses against theoretical 0.00819372; hash/query distributions cause sampling differences.

11. **Can Bloom say absent for a present key?** `Bloom::Add` sets the same `k` positions tested by `MayContain`, so no under intact filter bytes. A corrupted SST, including its filter, fails checksum validation on open. Bloom positives still require block lookup.

12. **Why this compaction strategy?** `DB::Compact` uses one count-triggered tier: it merges every live SSTable. Simpler than levels or size classes, with a direct proof that dropping tombstones is safe. Cost: all live data can be rewritten whenever threshold is hit; reads can probe several files until merge; peak disk use holds old and new files.

13. **How does merge choose newest value?** A priority queue emits smallest key. For equal keys its comparator ranks larger table index first; table IDs are ascending in MANIFEST. Other versions of that key are advanced and discarded. If winning record is a tombstone, it is omitted only because all live SSTables are in this merge.

14. **Why does a tombstone matter?** `Get` stops on a tombstone so older SST data cannot reappear. A tombstone written into a newer SST persists until full compaction removes it together with every older version. The dedicated test checks delete after an older flush, compaction, and reopen.

15. **What is not guaranteed?** No transactions, snapshots, concurrency, file locking, repair of damaged acknowledged data, or bounded RAM. Batched `Put` is not durable until `Sync` or flush rotation. A process continuing after general I/O failure may need reopen; WAL rejects further appends after its own I/O error. WSL2 host-level power-loss behavior was not tested.
