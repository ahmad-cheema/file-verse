#include "omni_core.hpp"
#include "odf_types.hpp"

#include <fstream>
#include <iostream>
#include <cstring>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <sstream>
#include <iomanip>

using namespace std;

static const uint32_t DEFAULT_MAX_USERS = 50;
static const uint64_t DEFAULT_TOTAL_SIZE = 104857600ULL; // 100MB
static const uint64_t DEFAULT_HEADER_SIZE = 512ULL;
static const uint64_t DEFAULT_BLOCK_SIZE = 4096ULL; // 4KB
constexpr size_t PWHASH_STORE = sizeof(((UserInfo*)0)->password_hash);

static bool write_at(const string& path, uint64_t offset, const void* data, size_t len) {
    std::fstream fs(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!fs.is_open()) return false;
    fs.seekp(offset);
    fs.write(reinterpret_cast<const char*>(data), len);
    fs.flush();
    return !fs.fail();
}

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

    {
        std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) return static_cast<int>(OFSErrorCodes::ERROR_IO_ERROR);
        if (total_size == 0) return static_cast<int>(OFSErrorCodes::ERROR_INVALID_CONFIG);
        ofs.seekp((std::streamoff)total_size - 1);
        char zero = 0;
        ofs.write(&zero, 1);
        ofs.close();
    }


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


    uint64_t user_table_size = (uint64_t)max_users * sizeof(UserInfo);
    uint64_t free_map_offset = user_table_offset + user_table_size;
    uint64_t remaining_for_blocks = 0;
    if (total_size > free_map_offset) remaining_for_blocks = total_size - free_map_offset;
    uint64_t num_blocks = remaining_for_blocks / block_size;

    if (!write_at(path, 0, &header, sizeof(header))) return static_cast<int>(OFSErrorCodes::ERROR_IO_ERROR);


    vector<char> zeros_u(user_table_size, 0);
    if (!write_at(path, user_table_offset, zeros_u.data(), zeros_u.size())) return static_cast<int>(OFSErrorCodes::ERROR_IO_ERROR);

    vector<uint8_t> free_map((size_t)num_blocks, 0);
    if (!write_at(path, free_map_offset, free_map.data(), free_map.size())) return static_cast<int>(OFSErrorCodes::ERROR_IO_ERROR);


    uint64_t file_table_offset = free_map_offset + num_blocks;
    uint32_t zero = 0;
    if (!write_at(path, file_table_offset, &zero, sizeof(zero))) return static_cast<int>(OFSErrorCodes::ERROR_IO_ERROR);

    return static_cast<int>(OFSErrorCodes::SUCCESS);
}

int fs_init(void** instance, const char* omni_path, const char* /*config_path*/) {
    if (!instance || !omni_path) return static_cast<int>(OFSErrorCodes::ERROR_INVALID_CONFIG);
    string path(omni_path);
    
    OMNIHeader header;
    if (!read_at(path, 0, &header, sizeof(header))) return static_cast<int>(OFSErrorCodes::ERROR_IO_ERROR);

    if (std::string(header.magic) != "OMNIFS01") return static_cast<int>(OFSErrorCodes::ERROR_INVALID_CONFIG);

    OFSInstance* inst = new OFSInstance();
    inst->header = header;
    inst->omni_path = path;
    inst->max_users = header.max_users;
    inst->block_size = header.block_size;

    uint64_t user_table_offset = header.user_table_offset;
    uint64_t user_table_size = (uint64_t)inst->max_users * sizeof(UserInfo);
    inst->users.resize(inst->max_users);
    {
        std::ifstream fs(path, std::ios::in | std::ios::binary);
        if (!fs.is_open()) { delete inst; return static_cast<int>(OFSErrorCodes::ERROR_IO_ERROR); }
        fs.seekg(user_table_offset);
        fs.read(reinterpret_cast<char*>(inst->users.data()), user_table_size);
    }

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

    inst->content_offset = free_map_offset + inst->num_blocks;

    *instance = inst;
    return static_cast<int>(OFSErrorCodes::SUCCESS);
}

void fs_shutdown(void* instance) {
    if (!instance) return;
    OFSInstance* inst = reinterpret_cast<OFSInstance*>(instance);
    
    if (inst->dirty) {
        uint64_t user_table_offset = inst->header.user_table_offset;
        uint64_t user_table_size = (uint64_t)inst->max_users * sizeof(UserInfo);
        write_at(inst->omni_path, user_table_offset, inst->users.data(), user_table_size);

        uint64_t free_map_offset = user_table_offset + user_table_size;
        if (!inst->free_map.empty())
            write_at(inst->omni_path, free_map_offset, inst->free_map.data(), inst->free_map.size());
        
        uint64_t file_table_offset = free_map_offset + inst->num_blocks;
        uint32_t count = (uint32_t)inst->files.size();
        write_at(inst->omni_path, file_table_offset, &count, sizeof(count));
        uint64_t cursor = file_table_offset + sizeof(count);
        for (const auto &f : inst->files) {
            write_at(inst->omni_path, cursor, &f.entry, sizeof(FileEntry));
            cursor += sizeof(FileEntry);
            
            uint32_t bc = (uint32_t)f.blocks.size();
            write_at(inst->omni_path, cursor, &bc, sizeof(bc));
            cursor += sizeof(bc);
            
            if (bc > 0) {
                write_at(inst->omni_path, cursor, f.blocks.data(), sizeof(uint32_t) * bc);
                cursor += sizeof(uint32_t) * bc;
            }
        }
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

    int slot = -1;
    for (size_t i = 0; i < inst->users.size(); ++i) {
        if (!inst->users[i].is_active) { slot = (int)i; break; }
    }
    if (slot < 0) return static_cast<int>(OFSErrorCodes::ERROR_NO_SPACE);

    UserInfo nu;
    std::memset(&nu, 0, sizeof(nu));
    std::strncpy(nu.username, username, sizeof(nu.username) - 1);
    nu.role = role;
    nu.created_time = static_cast<uint64_t>(std::time(nullptr));
    
    auto compute_hash = [](const std::string& uname, const std::string& pw, uint64_t created)->std::string{
        std::string seed = uname + ":" + pw + ":" + std::to_string(created);
        // use std::hash repeatedly to build 64 hex chars
        std::ostringstream oss;
        size_t h = std::hash<std::string>{}(seed);
        for (int i = 0; i < 8; ++i) {
            h = std::hash<size_t>{}(h ^ (i + 0x9e3779b97f4a7c15ULL));
            oss << std::hex << std::setw(16) << std::setfill('0') << (uint64_t)h;
        }
        std::string out = oss.str();
        if (out.size() > sizeof(nu.password_hash) - 1) out.resize(sizeof(nu.password_hash) - 1);
        return out;
    };
    std::string ph = compute_hash(std::string(username), std::string(password), nu.created_time);
    std::strncpy(nu.password_hash, ph.c_str(), sizeof(nu.password_hash) - 1);
    nu.last_login = 0;
    nu.is_active = 1;


    inst->users[slot] = nu;
    inst->user_index.insert(std::string(nu.username), slot);
    inst->dirty = true;

    write_at(inst->omni_path, inst->header.user_table_offset, inst->users.data(), (size_t)inst->max_users * sizeof(UserInfo));

    return static_cast<int>(OFSErrorCodes::SUCCESS);
}

int user_login(void* instance_ptr, void** session, const char* username, const char* password) {
    if (!instance_ptr || !session || !username || !password) return static_cast<int>(OFSErrorCodes::ERROR_INVALID_OPERATION);
    OFSInstance* inst = reinterpret_cast<OFSInstance*>(instance_ptr);

    int slot = inst->user_index.find(std::string(username));
    if (slot < 0) return static_cast<int>(OFSErrorCodes::ERROR_NOT_FOUND);
    const UserInfo& u = inst->users[(size_t)slot];
    if (!u.is_active) return static_cast<int>(OFSErrorCodes::ERROR_NOT_FOUND);
    
    auto compute_hash = [](const std::string& uname, const std::string& pw, uint64_t created)->std::string{
        std::string seed = uname + ":" + pw + ":" + std::to_string(created);
        std::ostringstream oss;
        size_t h = std::hash<std::string>{}(seed);
        for (int i = 0; i < 8; ++i) {
            h = std::hash<size_t>{}(h ^ (i + 0x9e3779b97f4a7c15ULL));
            oss << std::hex << std::setw(16) << std::setfill('0') << (uint64_t)h;
        }
        std::string out = oss.str();
        if (out.size() > PWHASH_STORE - 1) out.resize(PWHASH_STORE - 1);
        return out;
    };
    std::string expected = compute_hash(std::string(u.username), std::string(password), u.created_time);
    if (expected != std::string(u.password_hash)) {
        return static_cast<int>(OFSErrorCodes::ERROR_PERMISSION_DENIED);
    }

    uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    std::ostringstream sid;
    sid << "tok-" << std::hex << now << "-" << slot;
    std::string token = sid.str();
    SessionInfo sobj(token, u, now);
    
    {
        std::lock_guard<std::mutex> lg(inst->mutex);
        inst->sessions.push_back(sobj);
    }
    // return a heap-allocated SessionInfo copy to caller (caller owns)
    SessionInfo* out = new SessionInfo(token, u, now);
    *session = out;
    return static_cast<int>(OFSErrorCodes::SUCCESS);
}

int get_session_by_token(void* instance_ptr, const char* token, void** session_out) {
    if (!instance_ptr || !token || !session_out) return static_cast<int>(OFSErrorCodes::ERROR_INVALID_OPERATION);
    OFSInstance* inst = reinterpret_cast<OFSInstance*>(instance_ptr);
    std::lock_guard<std::mutex> lg(inst->mutex);
    for (auto &s : inst->sessions) {
        if (std::string(s.session_id) == std::string(token)) {
            // update last_activity
            s.last_activity = static_cast<uint64_t>(std::time(nullptr));
            // return a copy allocated on heap (caller should not delete internal storage)
            SessionInfo* out = new SessionInfo(std::string(s.session_id), s.user, s.login_time);
            *session_out = out;
            return static_cast<int>(OFSErrorCodes::SUCCESS);
        }
    }
    return static_cast<int>(OFSErrorCodes::ERROR_INVALID_SESSION);
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

// Helper: allocate N free blocks (non-contiguous) and return indices in out vector
static bool allocate_blocks(OFSInstance* inst, size_t n, std::vector<uint32_t>& out) {
    out.clear();
    if (n == 0) return true;
    for (uint32_t i = 0; i < inst->free_map.size() && out.size() < n; ++i) {
        if (inst->free_map[i] == 0) {
            inst->free_map[i] = 1;
            out.push_back(i);
        }
    }
    if (out.size() < n) {
        // rollback
        for (uint32_t idx : out) inst->free_map[idx] = 0;
        out.clear();
        return false;
    }
    inst->dirty = true;
    return true;
}

static void free_blocks(OFSInstance* inst, const std::vector<uint32_t>& blocks) {
    for (uint32_t b : blocks) {
        if (b < inst->free_map.size()) inst->free_map[b] = 0;
    }
    inst->dirty = true;
}

// Persist the in-memory file table to disk (writes count, entries and block lists)
static bool write_file_table(OFSInstance* inst) {
    if (!inst) return false;
    uint64_t file_table_offset = inst->header.file_state_storage_offset;
    uint64_t meta_end = inst->header.change_log_offset;
    if (meta_end <= file_table_offset) return false;
    uint64_t meta_size = meta_end - file_table_offset;
    // write count then iterate
    uint32_t count = (uint32_t)inst->files.size();
    if (!write_at(inst->omni_path, file_table_offset, &count, sizeof(count))) return false;
    uint64_t cursor = file_table_offset + sizeof(count);
    for (const auto &f : inst->files) {
        if (cursor + sizeof(FileEntry) > meta_end) return false;
        if (!write_at(inst->omni_path, cursor, &f.entry, sizeof(FileEntry))) return false;
        cursor += sizeof(FileEntry);
        uint32_t bc = (uint32_t)f.blocks.size();
        if (cursor + sizeof(bc) > meta_end) return false;
        if (!write_at(inst->omni_path, cursor, &bc, sizeof(bc))) return false;
        cursor += sizeof(bc);
        if (bc > 0) {
            if (cursor + sizeof(uint32_t) * bc > meta_end) return false;
            if (!write_at(inst->omni_path, cursor, f.blocks.data(), sizeof(uint32_t) * bc)) return false;
            cursor += sizeof(uint32_t) * bc;
        }
    }
    return true;
}

// Load file table from disk into inst->files
static bool read_file_table(OFSInstance* inst) {
    if (!inst) return false;
    uint64_t file_table_offset = inst->header.file_state_storage_offset;
    uint64_t meta_end = inst->header.change_log_offset;
    if (meta_end <= file_table_offset) return true; // nothing reserved
    uint64_t meta_size = meta_end - file_table_offset;
    uint32_t count = 0;
    if (!read_at(inst->omni_path, file_table_offset, &count, sizeof(count))) return false;
    uint64_t cursor = file_table_offset + sizeof(count);
    inst->files.clear();
    for (uint32_t i = 0; i < count; ++i) {
        if (cursor + sizeof(FileEntry) > meta_end) return false;
        FileEntry fe;
        if (!read_at(inst->omni_path, cursor, &fe, sizeof(FileEntry))) return false;
        cursor += sizeof(FileEntry);
        uint32_t bc = 0;
        if (!read_at(inst->omni_path, cursor, &bc, sizeof(bc))) return false;
        cursor += sizeof(bc);
        std::vector<uint32_t> blocks(bc);
        if (bc > 0) {
            if (!read_at(inst->omni_path, cursor, blocks.data(), sizeof(uint32_t) * bc)) return false;
            cursor += sizeof(uint32_t) * bc;
        }
        OFSInstance::InMemoryFile imf;
        imf.path = std::string(fe.name);
        imf.entry = fe;
        imf.blocks = std::move(blocks);
        inst->files.push_back(std::move(imf));
    }
    return true;
}

int file_create(void* instance, void* session, const char* path, const char* data, size_t size) {
    if (!instance || !path) return static_cast<int>(OFSErrorCodes::ERROR_INVALID_OPERATION);
    OFSInstance* inst = reinterpret_cast<OFSInstance*>(instance);
    // allocate blocks
    size_t blocks_needed = (size + inst->block_size - 1) / inst->block_size;
    std::vector<uint32_t> blocks;
    if (!allocate_blocks(inst, blocks_needed, blocks)) return static_cast<int>(OFSErrorCodes::ERROR_NO_SPACE);

    // write data to blocks
    size_t remaining = size;
    const char* ptr = data;
    for (size_t i = 0; i < blocks.size(); ++i) {
        uint32_t bidx = blocks[i];
        uint64_t off = inst->content_offset + (uint64_t)bidx * inst->block_size;
        size_t chunk = remaining > inst->block_size ? inst->block_size : remaining;
        if (!write_at(inst->omni_path, off, ptr, chunk)) {
            // rollback
            free_blocks(inst, blocks);
            return static_cast<int>(OFSErrorCodes::ERROR_IO_ERROR);
        }
        ptr += chunk; remaining -= chunk;
    }

    FileEntry fe;
    std::memset(&fe, 0, sizeof(fe));
    std::strncpy(fe.name, path, sizeof(fe.name)-1);
    fe.type = static_cast<uint8_t>(EntryType::FILE);
    fe.size = size;
    fe.permissions = 0644;
    fe.created_time = static_cast<uint64_t>(std::time(nullptr));
    fe.modified_time = fe.created_time;
    // owner from session if provided
    if (session) {
        SessionInfo* s = reinterpret_cast<SessionInfo*>(session);
        std::strncpy(fe.owner, s->user.username, sizeof(fe.owner)-1);
    }
    fe.inode = (uint32_t)inst->files.size() + 1;

    OFSInstance::InMemoryFile imf;
    imf.path = std::string(path);
    imf.entry = fe;
    imf.blocks = blocks;
    inst->files.push_back(std::move(imf));
    inst->dirty = true;
    // persist file table and free_map
    write_at(inst->omni_path, inst->header.user_table_offset + inst->max_users * sizeof(UserInfo), inst->free_map.data(), inst->free_map.size());
    write_file_table(inst);
    return static_cast<int>(OFSErrorCodes::SUCCESS);
}

// Helper function to check if session user owns the file
static bool check_file_permission(const FileEntry& entry, void* session) {
    if (!session) return false; // No session = no access
    SessionInfo* s = reinterpret_cast<SessionInfo*>(session);
    // Admin can access everything
    if (s->user.role == UserRole::ADMIN) return true;
    // Check if owner matches
    return std::strcmp(entry.owner, s->user.username) == 0;
}

int file_read(void* instance, void* session, const char* path, char** buffer, size_t* size_out) {
    if (!instance || !path || !buffer || !size_out) return static_cast<int>(OFSErrorCodes::ERROR_INVALID_OPERATION);
    OFSInstance* inst = reinterpret_cast<OFSInstance*>(instance);
    for (const auto &f : inst->files) {
        if (f.path == path && f.entry.getType() == EntryType::FILE) {
            // Check permission
            if (!check_file_permission(f.entry, session)) {
                return static_cast<int>(OFSErrorCodes::ERROR_PERMISSION_DENIED);
            }
            size_t total = (size_t)f.entry.size;
            char* buf = new char[total];
            size_t copied = 0;
            for (size_t i = 0; i < f.blocks.size(); ++i) {
                uint32_t bidx = f.blocks[i];
                uint64_t off = inst->content_offset + (uint64_t)bidx * inst->block_size;
                size_t chunk = std::min((size_t)inst->block_size, total - copied);
                std::ifstream ifs(inst->omni_path, std::ios::in | std::ios::binary);
                ifs.seekg(off);
                ifs.read(buf + copied, chunk);
                copied += chunk;
            }
            *buffer = buf;
            *size_out = total;
            return static_cast<int>(OFSErrorCodes::SUCCESS);
        }
    }
    return static_cast<int>(OFSErrorCodes::ERROR_NOT_FOUND);
}

int file_delete(void* instance, void* session, const char* path) {
    if (!instance || !path) return static_cast<int>(OFSErrorCodes::ERROR_INVALID_OPERATION);
    OFSInstance* inst = reinterpret_cast<OFSInstance*>(instance);
    for (size_t i = 0; i < inst->files.size(); ++i) {
        if (inst->files[i].path == path) {
            // Check permission
            if (!check_file_permission(inst->files[i].entry, session)) {
                return static_cast<int>(OFSErrorCodes::ERROR_PERMISSION_DENIED);
            }
            free_blocks(inst, inst->files[i].blocks);
            inst->files.erase(inst->files.begin() + i);
            inst->dirty = true;
            write_at(inst->omni_path, inst->header.user_table_offset + inst->max_users * sizeof(UserInfo), inst->free_map.data(), inst->free_map.size());
            write_file_table(inst);
            return static_cast<int>(OFSErrorCodes::SUCCESS);
        }
    }
    return static_cast<int>(OFSErrorCodes::ERROR_NOT_FOUND);
}

int file_exists(void* instance, void* /*session*/, const char* path) {
    if (!instance || !path) return static_cast<int>(OFSErrorCodes::ERROR_INVALID_OPERATION);
    OFSInstance* inst = reinterpret_cast<OFSInstance*>(instance);
    for (const auto &f : inst->files) if (f.path == path) return static_cast<int>(OFSErrorCodes::SUCCESS);
    return static_cast<int>(OFSErrorCodes::ERROR_NOT_FOUND);
}

int dir_create(void* instance, void* session, const char* path) {
    if (!instance || !path) return static_cast<int>(OFSErrorCodes::ERROR_INVALID_OPERATION);
    OFSInstance* inst = reinterpret_cast<OFSInstance*>(instance);
    FileEntry fe;
    std::memset(&fe, 0, sizeof(fe));
    std::strncpy(fe.name, path, sizeof(fe.name)-1);
    fe.type = static_cast<uint8_t>(EntryType::DIRECTORY);
    fe.size = 0;
    fe.permissions = 0755;
    fe.created_time = static_cast<uint64_t>(std::time(nullptr));
    fe.modified_time = fe.created_time;
    if (session) {
        SessionInfo* s = reinterpret_cast<SessionInfo*>(session);
        std::strncpy(fe.owner, s->user.username, sizeof(fe.owner)-1);
    }
    fe.inode = (uint32_t)inst->files.size() + 1;
    OFSInstance::InMemoryFile imf;
    imf.path = std::string(path);
    imf.entry = fe;
    inst->files.push_back(std::move(imf));
    inst->dirty = true;
    write_file_table(inst);
    return static_cast<int>(OFSErrorCodes::SUCCESS);
}

int dir_list(void* instance, void* session, const char* path, FileEntry** entries, int* count) {
    if (!instance || !path || !entries || !count) return static_cast<int>(OFSErrorCodes::ERROR_INVALID_OPERATION);
    OFSInstance* inst = reinterpret_cast<OFSInstance*>(instance);
    std::vector<FileEntry> found;
    std::string prefix(path);
    if (prefix.back() != '/') prefix += '/';
    for (const auto &f : inst->files) {
        std::string p = f.path;
        if (p == path) continue; // skip self
        if (p.rfind(prefix, 0) == 0) {
            // immediate child test
            std::string rest = p.substr(prefix.size());
            if (!rest.empty() && rest.find('/') == std::string::npos) {
                // Only show files the user owns (or all if admin)
                if (check_file_permission(f.entry, session)) {
                    found.push_back(f.entry);
                }
            }
        }
    }
    *count = (int)found.size();
    if (found.empty()) { *entries = nullptr; return static_cast<int>(OFSErrorCodes::SUCCESS); }
    FileEntry* out = new FileEntry[found.size()];
    for (size_t i = 0; i < found.size(); ++i) out[i] = found[i];
    *entries = out;
    return static_cast<int>(OFSErrorCodes::SUCCESS);
}
