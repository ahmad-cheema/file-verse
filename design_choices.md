# Design Choices — Part 1 (Core Design)

This document explains the data-structure and layout decisions made for the Part 1 core implementation. It is intentionally minimal for Phase 1, but follows the project constraints.

1) User indexing
- In-memory: unordered_map<string, UserInfo> maps username → UserInfo for O(1) lookup. This meets fast login requirements.
- On-disk: fixed-size user table located immediately after the 512-byte OMNIHeader. Each slot is a `UserInfo` struct (fixed size). The header stores `user_table_offset` and `max_users`.

Why: O(1) lookups are required for login; the in-memory hash map provides that. The fixed-size table simplifies serialization and binary compatibility.

2) Directory tree representation (design notes)
- Metadata index area (file_system_design.md) enforces fixed-size entries per file or directory. Each metadata entry contains parent index, short name, flags and a start-block index.
- Path traversal: to resolve `/a/b/c.txt` the system traverses metadata entries using parent-child links. For better performance in later phases we will augment with an in-memory path cache (hash map path → entry index).

3) File metadata indexing
- The Metadata Index Area is the authoritative store for file/directory entries. At startup we load the user table and free map; listing of metadata entries will be added in Phase 1 as needed.

4) Free space management
- Implementation uses a free-map (one byte per block) stored right after the user table. In-memory this is a vector<uint8_t> with 0=free,1=used. Finding N contiguous blocks is currently a linear scan; this will be improved to a first-fit / buddy-like mechanism if needed.

5) In-memory vs on-disk
- On startup (`fs_init`) the header, user table and free map are loaded into memory. User lookups then go to the in-memory hash map.
- Metadata entries and file contents are read on demand for performance and memory conservation.

Security / encoding
- The Phase 1 implementation stores passwords plainly for demonstration only (insecure). The final implementation must use SHA-256 and secure storage.

Notes and next steps
- This first-cut implementation focuses on wiring the file layout, basic user handling and a simple API. Next steps: implement metadata index loading, directory operations, block allocation, file read/write and the FIFO server.
