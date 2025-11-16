#include "fifo_server.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <cstring>
#include <sstream>

// Very small JSON helpers (parsing only operation and request_id fields)
static std::string extract_json_string(const std::string& json, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    auto pos = json.find(pat);
    if (pos == std::string::npos) return std::string();
    pos = json.find(':', pos);
    if (pos == std::string::npos) return std::string();
    pos++;
    // skip spaces
    while (pos < json.size() && isspace((unsigned char)json[pos])) pos++;
    if (pos < json.size() && json[pos] == '"') {
        pos++;
        size_t end = pos;
        while (end < json.size() && json[end] != '"') end++;
        return json.substr(pos, end - pos);
    }
    return std::string();
}

FIFOService::FIFOService(int port, OFSInstance* inst) : port_(port), instance_(inst) {}

FIFOService::~FIFOService() { stop(); }

bool FIFOService::start() {
    if (running_) return true;

    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) { perror("socket"); return false; }

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return false; }

    if (listen(server_fd_, 16) < 0) { perror("listen"); return false; }
    running_ = true;

    accept_thread_ = std::thread(&FIFOService::accept_loop, this);
    worker_thread_ = std::thread(&FIFOService::worker_loop, this);
    return true;
}

void FIFOService::stop() {
    if (!running_) return;
    running_ = false;
    shutdown(server_fd_, SHUT_RDWR);
    close(server_fd_);
    if (accept_thread_.joinable()) accept_thread_.join();
    // wake worker
    queue_cv_.notify_all();
    if (worker_thread_.joinable()) worker_thread_.join();
}

void FIFOService::accept_loop() {
    while (running_) {
        struct sockaddr_in cli;
        socklen_t len = sizeof(cli);
        int c = accept(server_fd_, (struct sockaddr*)&cli, &len);
        if (c < 0) {
            if (!running_) break;
            perror("accept");
            continue;
        }
        // make non-blocking
        int flags = fcntl(c, F_GETFL, 0);
        fcntl(c, F_SETFL, flags | O_NONBLOCK);
        std::thread(&FIFOService::reader_loop, this, c).detach();
    }
}

void FIFOService::reader_loop(int client_fd) {
    // Read until EOF or newline separated JSON messages
    std::string buf;
    char tmp[1024];
    while (running_) {
        ssize_t r = recv(client_fd, tmp, sizeof(tmp), 0);
        if (r > 0) {
            buf.append(tmp, tmp + r);
            // try to extract lines
            size_t pos;
            while ((pos = buf.find('\n')) != std::string::npos) {
                std::string line = buf.substr(0, pos);
                buf.erase(0, pos + 1);
                FSRequest req;
                req.raw = line;
                req.client_fd = client_fd;
                req.id = extract_json_string(line, "request_id");
                {
                    std::lock_guard<std::mutex> lg(queue_mutex_);
                    request_queue_.push_back(req);
                }
                queue_cv_.notify_one();
            }
        } else if (r == 0) {
            break; // client closed
        } else {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            break;
        }
    }
    close(client_fd);
}

// Worker thread: processes requests FIFO and sends JSON responses
void FIFOService::worker_loop() {
    while (running_) {
        FSRequest req;
        {
            std::unique_lock<std::mutex> ul(queue_mutex_);
            queue_cv_.wait(ul, [this]() { return !request_queue_.empty() || !running_; });
            if (!running_) break;
            req = request_queue_.front();
            request_queue_.pop_front();
        }

        // Process request: very small dispatcher that supports only ping and user_login and user_list
        std::string op = extract_json_string(req.raw, "operation");
        std::string resp;
        if (op == "ping") {
            resp = "{\"status\":\"success\",\"request_id\":\"" + req.id + "\",\"message\":\"pong\"}\n";
        } else if (op == "user_login") {
            // parameters: username, password
            std::string username = extract_json_string(req.raw, "username");
            std::string password = extract_json_string(req.raw, "password");
            // operate on instance_ directly (no env var)
            void* session = nullptr;
            int r = user_login(instance_, &session, username.c_str(), password.c_str());
            if (r == 0 && session) {
                resp = "{\"status\":\"success\",\"request_id\":\"" + req.id + "\"}\n";
                // session is allocated by user_login; we free it immediately for demo (not persistent)
                delete reinterpret_cast<SessionInfo*>(session);
            } else {
                resp = "{\"status\":\"error\",\"request_id\":\"" + req.id + "\",\"error\":\"login_failed\"}\n";
            }
        } else if (op == "user_list") {
            // expects admin session token? in this demo we skip session checks and list via env var helpers
            // We will attempt to call user_list using a fake admin session if possible
            SessionInfo adminSess;
            std::memset(&adminSess, 0, sizeof(adminSess));
            adminSess.user.role = UserRole::ADMIN;
            UserInfo* users = nullptr;
            int count = 0;
            int r = user_list(instance_, &adminSess, &users, &count);
            if (r == 0) {
                std::ostringstream oss;
                oss << "{\"status\":\"success\",\"request_id\":\"" << req.id << "\",\"users\":[";
                for (int i = 0; i < count; ++i) {
                    if (i) oss << ",";
                    oss << "{\"username\":\"" << users[i].username << "\",\"role\":\"" << (int)users[i].role << "\"}";
                }
                oss << "]}\n";
                resp = oss.str();
                delete [] users;
            } else {
                resp = "{\"status\":\"error\",\"request_id\":\"" + req.id + "\",\"error\":\"list_failed\"}\n";
            }
        } else {
            resp = "{\"status\":\"error\",\"request_id\":\"" + req.id + "\",\"error\":\"unknown_operation\"}\n";
        }

        // send response
        send(req.client_fd, resp.c_str(), resp.size(), 0);
    }
}
