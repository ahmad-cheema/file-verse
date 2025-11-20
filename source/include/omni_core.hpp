#ifndef OMNI_CORE_HPP
#define OMNI_CORE_HPP

#include "odf_types.hpp"
#include <string>
#include <vector>
#include <string>
#include <mutex>

/* C-style API */
extern "C" {
    int fs_init(void** instance, const char* omni_path, const char* config_path);
    void fs_shutdown(void* instance);
    int fs_format(const char* omni_path, const char* config_path);
    
    int user_create(void* instance, void* admin_session, const char* username, const char* password, UserRole role);
    int user_login(void* instance, void** session, const char* username, const char* password);
    int get_session_by_token(void* instance, const char* token, void** session_out);
    int user_list(void* instance, void* admin_session, UserInfo** users, int* count);
    int file_create(void* instance, void* session, const char* path, const char* data, size_t size);
    int file_read(void* instance, void* session, const char* path, char** buffer, size_t* size_out);
    int file_delete(void* instance, void* session, const char* path);
    int file_exists(void* instance, void* session, const char* path);
    int dir_create(void* instance, void* session, const char* path);
    int dir_list(void* instance, void* session, const char* path, FileEntry** entries, int* count);
}

/* Internal instance object and simple user index */
struct SimpleUserIndex {
    std::vector<std::string> keys;
    std::vector<int> values;
    std::vector<char> used;
    size_t capacity = 0;
    void init(size_t cap) {
        capacity = cap;
        keys.assign(capacity, std::string());
        values.assign(capacity, -1);
        used.assign(capacity, 0);
    }
    static uint64_t hash_str(const std::string& s) {
        uint64_t h = 5381;
        for (unsigned char c : s) h = ((h << 5) + h) + c;
        return h;
    }
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
    std::vector<UserInfo> users;
    SimpleUserIndex user_index;
    std::vector<uint8_t> free_map;
    uint64_t num_blocks = 0;
    uint64_t block_size = 0;
    std::vector<SessionInfo> sessions;
    std::mutex mutex;
    bool dirty = false;
    uint64_t content_offset = 0;
    struct InMemoryFile {
        std::string path;
        FileEntry entry;
        std::vector<uint32_t> blocks;
    };
    std::vector<InMemoryFile> files;
};

#endif
