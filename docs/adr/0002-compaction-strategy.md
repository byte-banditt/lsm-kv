# ADR 0002: Compaction strategy

## Context

Each flush creates a new immutable SSTable. Reads probe tables newest to oldest, so table count affects lookup work. The current store has one process, no levels, and no background compaction.

## Decision

Use a single-tier, count-triggered size-tiered policy. When live table count reaches `compact_after` (default 4), merge all live tables into one sorted SSTable using a priority queue. For duplicate keys, newest table ID wins. Drop tombstones only because every live table participates. Persist the replacement table and MANIFEST before deleting old files. Do not use leveled compaction: it would require level placement and partial-range overlap management that this implementation does not have; the existing full merge fits its small, single-tier structure.

## Consequences

The policy has no size classes or levels. It bounds table probes between merges, but rewrites all live data at each compaction and temporarily needs space for both old and new tables. Compaction and `Scan` materialize table contents in memory. If every entry is a tombstone, compaction writes an empty MANIFEST and removes the old tables.
