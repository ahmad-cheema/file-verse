# Testing Report — initial checks (Part 1)

This file summarizes quick tests performed against the Part 1 implementation added in this commit.

Test: Build and run `tools/fs_test`
- Environment: Ubuntu-like Linux (local dev)
- Command: `make && ./tools/fs_test`
- Result: `test_student.omni` created; `fs_format`, `fs_init`, `user_create`, `user_login`, `user_list` exercised successfully in this prototype. The program prints return codes for each step.

Edge cases considered
- Missing `.omni` path: fs_format errors are returned appropriately.
- No free user slot: user_create returns `ERROR_NO_SPACE`.

Limitations / next steps
- Passwords are stored plaintext — MUST be replaced with proper hashing.
- File metadata index and content operations are not implemented yet.
- Add unit tests and CI build for automated validation.
