# File I/O Strategy (Part 1)

This document describes how the code reads/writes the `.omni` container at a high level.

Layout recap
- Byte 0..511: OMNIHeader (512 bytes fixed)
- user_table_offset (header) → user table (fixed slots of `UserInfo`)
- free map → contiguous bytes, one byte per content block
- content blocks → remainder of file, fixed-size blocks

Serialization / Deserialization
- The implementation writes C++ POD structs directly with binary writes (e.g. `ofstream.write(reinterpret_cast<const char*>(&header), sizeof(header))`). This keeps on-disk layout simple and binary-compatible across implementations as long as the definition sizes match.

Buffering strategy
- Startup (`fs_init`) loads the header, user table and free map into memory. These are small and allow fast operations (user lookup, free-block scanning).
- File contents and metadata entries are read and written on demand to keep memory usage reasonable.

File growth and allocation
- `fs_format` pre-allocates the file to the configured `total_size` (sparse-friendly). All offsets are computed relative to the header.
- Block allocation: the free map is a simple byte array; allocation is a first-fit scan for blocks. Each content block reserves its first 4 bytes for a `next_block` pointer (block index) so large files form a chain.

Data integrity
- The current implementation is a basic prototype: partial writes during crashes are possible. The final system should add journaling or a write-ahead log to ensure atomic updates.

What is kept in memory vs read from disk per operation
- In memory: OMNIHeader, user table (vector), user_map (hash), free_map (vector of bytes).
- On-demand: metadata entries (metadata index area) and file content blocks.
