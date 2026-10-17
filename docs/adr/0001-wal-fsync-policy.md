# ADR 0001: WAL fsync policy

## Context

The store must recover acknowledged writes from WAL after a process crash while the OS remains running. `DB::Put` and `DB::Delete` append to WAL before updating the memtable. Durability depends on the selected `SyncPolicy` and the failure model.

## Decision

Use `PerWrite` by default. `Wal::Append` calls `fsync` after every record in this mode, before `DB::Put` or `DB::Delete` returns. In `Batched` mode, append does not sync; explicit `DB::Sync()` syncs the current WAL. A flush writes and syncs the SSTable and MANIFEST before rotating WAL. WAL replay accepts complete CRC-valid records, then truncates and syncs any invalid or incomplete suffix.

## Consequences

`PerWrite` makes a successful write wait for WAL `fsync`. `Batched` writes not yet synced or installed by a flush may be lost. `DB.CrashAckedWrites200Iterations` tests process-crash recovery via SIGKILL and reopen; it does not test OS crash or power loss. File and directory `fsync` calls request persistence, but actual power-loss behavior depends on the OS, filesystem, and storage device honoring them and is untested.
