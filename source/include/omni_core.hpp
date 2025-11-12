#ifndef OMNI_CORE_HPP
#define OMNI_CORE_HPP

#include "odf_types.hpp"
#include <string>
#include <vector>
#include <string>
#include <mutex>

// C-style API expected by the project
extern "C" {
    int fs_init(void** instance, const char* omni_path, const char* config_path);
    void fs_shutdown(void* instance);
    int fs_format(const char* omni_path, const char* config_path);

    // New: all user operations require an OFSInstance* to operate on the loaded filesystem
    int user_create(void* instance, void* admin_session, const char* username, const char* password, UserRole role);
    int user_login(void* instance, void** session, const char* username, const char* password);
    int user_list(void* instance, void* admin_session, UserInfo** users, int* count);
}

// Internal instance object stored as opaque pointer by the C API
// Simple open-addressing hash index mapping username -> user table index
struct SimpleUserIndex {
    std::vector<std::string> keys; // key strings
    std::vector<int> values;       // corresponding user slot index
    std::vector<char> used;        // 0=empty,1=used
    size_t capacity = 0;

    // initialize table with given capacity (should be > max_users)
    void init(size_t cap) {
        capacity = cap;
        keys.assign(capacity, std::string());
        values.assign(capacity, -1);
        used.assign(capacity, 0);
    }

    // simple djb2 hash
    static uint64_t hash_str(const std::string& s) {
        uint64_t h = 5381;
        for (unsigned char c : s) h = ((h << 5) + h) + c;
        return h;
    }

    // insert or overwrite
    void insert(const std::string& key, int val) {
        if (capacity == 0) return;
        uint64_t h = hash_str(key);
        size_t idx = (size_t)(h % capacity);
        while (used[idx]) {
            if (keys[idx] == key) { values[idx] = val; return; }
            idx = (idx + 1) % capacity;
        }
        used[idx] = 1;
        keys[idx] = key;
        values[idx] = val;
    }

    // find value for key, or -1 if not found
    int find(const std::string& key) const {
        if (capacity == 0) return -1;
        uint64_t h = hash_str(key);
        size_t idx = (size_t)(h % capacity);
        size_t start = idx;
        while (used[idx]) {
            if (keys[idx] == key) return values[idx];
            idx = (idx + 1) % capacity;
            if (idx == start) break; // full loop
        }
        return -1;
    }
};

struct OFSInstance {
    OMNIHeader header;
    std::string omni_path;
    uint32_t max_users = 0;
    std::vector<UserInfo> users; // fixed-size table loaded from disk
    SimpleUserIndex user_index; // username -> user slot index
    std::vector<uint8_t> free_map; // 0=free,1=used per block
    uint64_t num_blocks = 0;
    uint64_t block_size = 0;
    std::mutex mutex;
    bool dirty = false;
};

#endif // OMNI_CORE_HPP
