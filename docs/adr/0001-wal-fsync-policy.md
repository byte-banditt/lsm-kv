# ADR 0001: WAL fsync policy

## Context

The store must recover acknowledged writes from WAL after a process crash. `DB::Put` and `DB::Delete` append to WAL before updating the memtable. Durability depends on the selected `SyncPolicy`.

## Decision

Use `PerWrite` by default. `Wal::Append` calls `fsync` after every record in this mode. In `Batched` mode, append does not sync; `Wal::Sync` is called by explicit `DB::Sync()` and as part of WAL rotation. Flush installs the SSTable and durable MANIFEST before rotating WAL. WAL replay accepts complete CRC-valid records, then truncates and syncs any invalid or incomplete suffix.

## Consequences

`PerWrite` makes a successful write wait for WAL `fsync`. A successful `Batched` write alone has no crash-durability guarantee; callers choose when to call `Sync()`. WAL rotation syncs its replacement before rename and syncs the directory after rename. Acknowledged-data recovery relies on the filesystem honoring `fsync` and rename.
