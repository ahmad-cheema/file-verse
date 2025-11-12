#include "omni_core.hpp"
#include "odf_types.hpp"

#include <fstream>
#include <iostream>
#include <cstring>
#include <chrono>
#include <ctime>
#include <algorithm>

using namespace std;

static const uint32_t DEFAULT_MAX_USERS = 50;
static const uint64_t DEFAULT_TOTAL_SIZE = 104857600ULL; // 100MB
static const uint64_t DEFAULT_HEADER_SIZE = 512ULL;
static const uint64_t DEFAULT_BLOCK_SIZE = 4096ULL; // 4KB

// Helper: write raw bytes at offset
static bool write_at(const string& path, uint64_t offset, const void* data, size_t len) {
    std::fstream fs(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!fs.is_open()) return false;
    fs.seekp(offset);
    fs.write(reinterpret_cast<const char*>(data), len);
    fs.flush();
    return !fs.fail();
}

// Helper: read raw bytes at offset
static bool read_at(const string& path, uint64_t offset, void* data, size_t len) {
    std::ifstream fs(path, std::ios::in | std::ios::binary);
    if (!fs.is_open()) return false;
    fs.seekg(offset);
    fs.read(reinterpret_cast<char*>(data), len);
    return !fs.fail();
}

int fs_format(const char* omni_path, const char* /*config_path*/) {
    if (!omni_path) return static_cast<int>(OFSErrorCodes::ERROR_INVALID_CONFIG);
    string path(omni_path);

    uint64_t total_size = DEFAULT_TOTAL_SIZE;
    uint64_t header_size = DEFAULT_HEADER_SIZE;
    uint64_t block_size = DEFAULT_BLOCK_SIZE;
    uint32_t max_users = DEFAULT_MAX_USERS;

    // Create/truncate file and allocate size (sparse-friendly)
    {
        std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) return static_cast<int>(OFSErrorCodes::ERROR_IO_ERROR);
        // allocate by seeking to total_size-1 and writing a zero byte
        if (total_size == 0) return static_cast<int>(OFSErrorCodes::ERROR_INVALID_CONFIG);
        ofs.seekp((std::streamoff)total_size - 1);
        char zero = 0;
        ofs.write(&zero, 1);
        ofs.close();
    }

    // Prepare header
    OMNIHeader header;
    std::memset(&header, 0, sizeof(header));
    std::strncpy(header.magic, "OMNIFS01", sizeof(header.magic));
    header.format_version = 0x00010000;
    header.total_size = total_size;
    header.header_size = header_size;
    header.block_size = block_size;
    std::strncpy(header.student_id, "", sizeof(header.student_id));
    std::strncpy(header.submission_date, "", sizeof(header.submission_date));

    uint32_t user_table_offset = static_cast<uint32_t>(header_size);
    header.user_table_offset = user_table_offset;
    header.max_users = max_users;

    // compute sizes
    uint64_t user_table_size = (uint64_t)max_users * sizeof(UserInfo);
    uint64_t free_map_offset = user_table_offset + user_table_size;
    uint64_t remaining_for_blocks = 0;
    if (total_size > free_map_offset) remaining_for_blocks = total_size - free_map_offset;
    uint64_t num_blocks = remaining_for_blocks / block_size;

    // We will store free_map immediately after user table (one byte per block)
    // Write header, empty user table, and free map
    if (!write_at(path, 0, &header, sizeof(header))) return static_cast<int>(OFSErrorCodes::ERROR_IO_ERROR);

    // Write zeroed user table
    vector<char> zeros_u(user_table_size, 0);
    if (!write_at(path, user_table_offset, zeros_u.data(), zeros_u.size())) return static_cast<int>(OFSErrorCodes::ERROR_IO_ERROR);

    // Write free map (all free)
    vector<uint8_t> free_map((size_t)num_blocks, 0);
    if (!write_at(path, free_map_offset, free_map.data(), free_map.size())) return static_cast<int>(OFSErrorCodes::ERROR_IO_ERROR);

    return static_cast<int>(OFSErrorCodes::SUCCESS);
}

int fs_init(void** instance, const char* omni_path, const char* /*config_path*/) {
    if (!instance || !omni_path) return static_cast<int>(OFSErrorCodes::ERROR_INVALID_CONFIG);
    string path(omni_path);
    // Read header
    OMNIHeader header;
    if (!read_at(path, 0, &header, sizeof(header))) return static_cast<int>(OFSErrorCodes::ERROR_IO_ERROR);

    // Basic validation
    if (std::string(header.magic) != "OMNIFS01") return static_cast<int>(OFSErrorCodes::ERROR_INVALID_CONFIG);

    OFSInstance* inst = new OFSInstance();
    inst->header = header;
    inst->omni_path = path;
    inst->max_users = header.max_users;
    inst->block_size = header.block_size;

    // Load user table
    uint64_t user_table_offset = header.user_table_offset;
    uint64_t user_table_size = (uint64_t)inst->max_users * sizeof(UserInfo);
    inst->users.resize(inst->max_users);
    {
        std::ifstream fs(path, std::ios::in | std::ios::binary);
        if (!fs.is_open()) { delete inst; return static_cast<int>(OFSErrorCodes::ERROR_IO_ERROR); }
        fs.seekg(user_table_offset);
        fs.read(reinterpret_cast<char*>(inst->users.data()), user_table_size);
    }

    // Build simple user index for quick lookup (username -> slot index)
    // choose table capacity as next power-of-two >= max_users * 2
    size_t cap = 1;
    while (cap < inst->max_users * 2) cap <<= 1;
    inst->user_index.init(cap);
    for (size_t i = 0; i < inst->users.size(); ++i) {
        const auto& u = inst->users[i];
        if (u.is_active) {
            std::string uname(u.username);
            inst->user_index.insert(uname, (int)i);
        }
    }

    // Load free map
    uint64_t free_map_offset = user_table_offset + user_table_size;
    uint64_t remaining_for_blocks = 0;
    if (header.total_size > free_map_offset) remaining_for_blocks = header.total_size - free_map_offset;
    uint64_t num_blocks = remaining_for_blocks / header.block_size;
    inst->num_blocks = num_blocks;
    inst->free_map.resize((size_t)num_blocks);
    if (num_blocks > 0) {
        std::ifstream fs(path, std::ios::in | std::ios::binary);
        if (!fs.is_open()) { delete inst; return static_cast<int>(OFSErrorCodes::ERROR_IO_ERROR); }
        fs.seekg(free_map_offset);
        fs.read(reinterpret_cast<char*>(inst->free_map.data()), (std::streamsize)num_blocks);
    }

    *instance = inst;
    return static_cast<int>(OFSErrorCodes::SUCCESS);
}

void fs_shutdown(void* instance) {
    if (!instance) return;
    OFSInstance* inst = reinterpret_cast<OFSInstance*>(instance);
    // If dirty, write back user table and free_map
    if (inst->dirty) {
        uint64_t user_table_offset = inst->header.user_table_offset;
        uint64_t user_table_size = (uint64_t)inst->max_users * sizeof(UserInfo);
        write_at(inst->omni_path, user_table_offset, inst->users.data(), user_table_size);

        uint64_t free_map_offset = user_table_offset + user_table_size;
        if (!inst->free_map.empty())
            write_at(inst->omni_path, free_map_offset, inst->free_map.data(), inst->free_map.size());
    }
    delete inst;
}

int user_create(void* instance_ptr, void* admin_session, const char* username, const char* password, UserRole role) {
    if (!instance_ptr || !username || !password) return static_cast<int>(OFSErrorCodes::ERROR_INVALID_OPERATION);
    OFSInstance* inst = reinterpret_cast<OFSInstance*>(instance_ptr);

    // Authorization: allow bootstrap (no admin_session) if no active users exist.
    bool is_admin_request = false;
    if (admin_session) {
        SessionInfo* s = reinterpret_cast<SessionInfo*>(admin_session);
        if (s->user.role == UserRole::ADMIN) is_admin_request = true;
        else return static_cast<int>(OFSErrorCodes::ERROR_PERMISSION_DENIED);
    } else {
        // If no active users exist, allow creating the first admin user
        bool any_active = false;
        for (const auto& u : inst->users) if (u.is_active) { any_active = true; break; }
        if (!any_active) is_admin_request = true; // bootstrap allowed
    }

    // Find free slot in inst->users
    int slot = -1;
    for (size_t i = 0; i < inst->users.size(); ++i) {
        if (!inst->users[i].is_active) { slot = (int)i; break; }
    }
    if (slot < 0) return static_cast<int>(OFSErrorCodes::ERROR_NO_SPACE);

    UserInfo nu;
    std::memset(&nu, 0, sizeof(nu));
    std::strncpy(nu.username, username, sizeof(nu.username) - 1);
    std::strncpy(nu.password_hash, password, sizeof(nu.password_hash) - 1); // NOTE: plaintext for now
    nu.role = role;
    nu.created_time = static_cast<uint64_t>(std::time(nullptr));
    nu.last_login = 0;
    nu.is_active = 1;

    // Update in-memory structures
    inst->users[slot] = nu;
    inst->user_index.insert(std::string(nu.username), slot);
    inst->dirty = true;

    return static_cast<int>(OFSErrorCodes::SUCCESS);
}

int user_login(void* instance_ptr, void** session, const char* username, const char* password) {
    if (!instance_ptr || !session || !username || !password) return static_cast<int>(OFSErrorCodes::ERROR_INVALID_OPERATION);
    OFSInstance* inst = reinterpret_cast<OFSInstance*>(instance_ptr);

    int slot = inst->user_index.find(std::string(username));
    if (slot < 0) return static_cast<int>(OFSErrorCodes::ERROR_NOT_FOUND);
    const UserInfo& u = inst->users[(size_t)slot];
    if (!u.is_active) return static_cast<int>(OFSErrorCodes::ERROR_NOT_FOUND);
    // plaintext compare for now
    if (std::string(u.password_hash) != password) return static_cast<int>(OFSErrorCodes::ERROR_PERMISSION_DENIED);

    SessionInfo* s = new SessionInfo(std::string("sess-") + username, u, static_cast<uint64_t>(std::time(nullptr)));
    *session = s;
    return static_cast<int>(OFSErrorCodes::SUCCESS);
}

int user_list(void* instance_ptr, void* admin_session, UserInfo** users_out, int* count) {
    if (!instance_ptr || !admin_session || !users_out || !count) return static_cast<int>(OFSErrorCodes::ERROR_INVALID_OPERATION);
    OFSInstance* inst = reinterpret_cast<OFSInstance*>(instance_ptr);
    SessionInfo* sess = reinterpret_cast<SessionInfo*>(admin_session);
    if (sess->user.role != UserRole::ADMIN) return static_cast<int>(OFSErrorCodes::ERROR_PERMISSION_DENIED);

    int c = 0;
    for (const auto& u : inst->users) if (u.is_active) ++c;
    *count = c;
    if (c == 0) { *users_out = nullptr; return static_cast<int>(OFSErrorCodes::SUCCESS); }

    UserInfo* result = new UserInfo[c];
    int idx = 0;
    for (const auto& u : inst->users) {
        if (!u.is_active) continue;
        result[idx++] = u;
    }
    *users_out = result;
    return static_cast<int>(OFSErrorCodes::SUCCESS);
}
