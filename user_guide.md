# User Guide (Part 1)

Quick start (build & run the small test):

1. Build:

```
make
```

2. Run the test binary (creates `test_student.omni` in repo root):

```
./tools/fs_test
```

What the test does
- Calls `fs_format("test_student.omni")` to create a fresh container.
- Calls `fs_init` to load header/user table/free map.
- Creates a user `admin` with password `admin123`.
- Logs in as `admin` and lists users.

Environment
- The test program sets the `FILEVERSE_OMNI` environment variable internally. Other utilities in this directory use `FILEVERSE_OMNI` to find the `.omni` container when performing user helpers.

Notes / Caveats
- Passwords are stored plainly in this initial implementation (for demonstrative purposes). Replace with secure hashing (SHA-256 + salt) before any real use.
- Many file/directory functions are placeholders in this iteration. This part focuses on the container layout, user table and basic IO patterns.
