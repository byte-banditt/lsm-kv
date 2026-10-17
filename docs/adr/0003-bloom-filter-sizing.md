# ADR 0003: Bloom filter sizing

## Context

SSTable Bloom filters avoid block reads for absent keys. `Options::bloom_bits_per_key` defaults to 10 and is passed to each table at creation.

## Decision

Use 10 bits per key by default. `Bloom` rounds `bits_per_key * ln(2)` to choose hash count, clamped to 1–30; at 10 bits this is 7 hashes. The bit count is at least 64 and rounded up to a whole byte. The filter uses double hashing with FNV-1a and a mixed odd step. The theoretical false-positive estimate is `(1 - exp(-kn/m))^k`. Re-run the deterministic test with 10,000 inserted keys and 100,000 absent queries; it measured 0.00703 versus theoretical 0.00819372.

## Consequences

The default allocates about 10 bits per key, plus rounding and the minimum filter size. More bits reduce expected false positives and increase SSTable size. The measured rate is specific to the test's deterministic keys and hash implementation; it is not a general workload guarantee.
