# FIFO Workflow (Design notes)

This document sketches the FIFO request-queue that will be used for the server in Part 2. It is intentionally implementation-agnostic for Phase 1.

Goal
- Guarantee operations are processed sequentially in arrival order to avoid concurrent modifications of the `.omni` file.

High-level approach
- A single queue (e.g. std::deque) will hold incoming JSON requests.
- A single worker thread will pop requests and execute them one-by-one against the in-memory FS instance.
- Network handlers only enqueue requests and wait for the worker to produce a JSON response.

Threading model
- Accepting connections: thread-per-connection or an event loop (epoll) — both enqueue requests.
- Worker thread processes items and writes responses back (via callback or shared socket data structure).

Why FIFO
- Simplicity: guarantees no two operations mutate the `.omni` state simultaneously, avoiding complex locking.
- Predictability: easy to reason about correctness and ordering.

Notes for implementation
- Keep request processing code idempotent where possible and ensure long-running operations can provide progress/timeouts.
