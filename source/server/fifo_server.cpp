#include "fifo_server.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <cstring>
#include <sstream>

static std::string extract_json_string(const std::string& json, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    auto pos = json.find(pat);
    if (pos == std::string::npos) return std::string();
    pos = json.find(':', pos);
    if (pos == std::string::npos) return std::string();
    pos++;
    
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
    char peekbuf[32] = {0};
    ssize_t pr = recv(c, peekbuf, sizeof(peekbuf) - 1, MSG_PEEK);
        if (pr > 0) {
            std::string s(peekbuf, (size_t)pr);
            if (s.rfind("POST ", 0) == 0 || s.rfind("GET ", 0) == 0 || s.rfind("OPTIONS ", 0) == 0) {
                std::thread(&FIFOService::handle_http_connection, this, c).detach();
                continue;
            }
        }
        int flags = fcntl(c, F_GETFL, 0);
        fcntl(c, F_SETFL, flags | O_NONBLOCK);
        std::thread(&FIFOService::reader_loop, this, c).detach();
    }
}

void FIFOService::handle_http_connection(int client_fd) {
    std::string headers;
    char buf[1024];
    ssize_t r;
    while (true) {
        r = recv(client_fd, buf, sizeof(buf), 0);
        if (r <= 0) { close(client_fd); return; }
        headers.append(buf, buf + r);
        size_t pos = headers.find("\r\n\r\n");
        if (pos != std::string::npos) {
            std::string hdrs = headers.substr(0, pos + 4);
            std::string rest = headers.substr(pos + 4);
            // parse request line to check method and path
            std::istringstream hs(hdrs);
            std::string request_line;
            std::getline(hs, request_line);
            // trim CR if present
            if (!request_line.empty() && request_line.back() == '\r') request_line.pop_back();
            std::istringstream rl(request_line);
            std::string method, path, proto;
            rl >> method >> path >> proto;
            // handle CORS preflight
            if (method == "OPTIONS") {
                std::string pre = "HTTP/1.1 204 No Content\r\n";
                pre += "Access-Control-Allow-Origin: *\r\n";
                pre += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
                pre += "Access-Control-Allow-Headers: Content-Type\r\n";
                pre += "Content-Length: 0\r\n";
                pre += "Connection: close\r\n\r\n";
                send(client_fd, pre.c_str(), pre.size(), 0);
                close(client_fd);
                return;
            }
            std::string cl_key = "Content-Length:";
            size_t clpos = hdrs.find(cl_key);
            int content_length = 0;
            if (clpos != std::string::npos) {
                size_t lineend = hdrs.find('\r', clpos);
                std::string clv = hdrs.substr(clpos + cl_key.size(), lineend - (clpos + cl_key.size()));
                try { content_length = std::stoi(clv); } catch(...) { content_length = 0; }
            }
            // Log the incoming HTTP request (method, path, content-length)
            std::cerr << "[HTTP] " << method << " " << path << " len=" << content_length << "\n";
            while ((int)rest.size() < content_length) {
                r = recv(client_fd, buf, sizeof(buf), 0);
                if (r <= 0) { close(client_fd); return; }
                rest.append(buf, buf + r);
            }
            std::string body = rest.substr(0, content_length);
            Pending pend;
            FSRequest req;
            req.raw = body;
            req.client_fd = client_fd;
            req.id = extract_json_string(body, "request_id");
            req.is_http = true;
            req.pending = &pend;
            {
                std::lock_guard<std::mutex> lg(queue_mutex_);
                request_queue_.push_back(req);
            }
            queue_cv_.notify_one();

            std::unique_lock<std::mutex> ul(pend.m);
            pend.cv.wait(ul, [&pend]() { return pend.done; });
            std::string resp_body = pend.response;
            std::ostringstream oss;
            oss << "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " << resp_body.size() << "\r\n\r\n";
            oss << resp_body;
            std::string out = oss.str();
            send(client_fd, out.c_str(), out.size(), 0);
            close(client_fd);
            return;
        }
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

    // Process request: dispatcher supports ping, user_login and user_list
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
                SessionInfo* s = reinterpret_cast<SessionInfo*>(session);
                std::string token(s->session_id);
                resp = "{\"status\":\"success\",\"request_id\":\"" + req.id + "\",\"token\":\"" + token + "\"}";
                delete s; // the core stored its own copy; this was a temporary copy
            } else {
                resp = "{\"status\":\"error\",\"request_id\":\"" + req.id + "\",\"error\":\"login_failed\"}";
            }
        } else if (op == "user_list") {
            // expect token in request body
            std::string token = extract_json_string(req.raw, "token");
            void* sessptr = nullptr;
            int r = get_session_by_token(instance_, token.c_str(), &sessptr);
            UserInfo* users = nullptr;
            int count = 0;
            if (r == 0 && sessptr) {
                r = user_list(instance_, sessptr, &users, &count);
                delete reinterpret_cast<SessionInfo*>(sessptr);
            } else {
                r = static_cast<int>(OFSErrorCodes::ERROR_INVALID_SESSION);
            }
            if (r == 0) {
                std::ostringstream oss;
                oss << "{\"status\":\"success\",\"request_id\":\"" << req.id << "\",\"users\":[";
                for (int i = 0; i < count; ++i) {
                    if (i) oss << ",";
                    oss << "{\"username\":\"" << users[i].username << "\",\"role\":\"" << (int)users[i].role << "\"}";
                }
                oss << "]}";
                resp = oss.str();
                delete [] users;
            } else {
                resp = "{\"status\":\"error\",\"request_id\":\"" + req.id + "\",\"error\":\"list_failed\"}";
            }
        } else {
            // File and directory operations: require token
            if (op == "file_create") {
                std::string token = extract_json_string(req.raw, "token");
                std::string path = extract_json_string(req.raw, "path");
                std::string data = extract_json_string(req.raw, "data");
                void* sessptr = nullptr;
                int gr = get_session_by_token(instance_, token.c_str(), &sessptr);
                if (gr != 0) {
                    resp = "{\"status\":\"error\",\"request_id\":\"" + req.id + "\",\"error\":\"invalid_session\"}";
                } else {
                    int cr = file_create(instance_, sessptr, path.c_str(), data.c_str(), data.size());
                    delete reinterpret_cast<SessionInfo*>(sessptr);
                    if (cr == 0) {
                        resp = "{\"status\":\"success\",\"request_id\":\"" + req.id + "\"}";
                    } else {
                        resp = "{\"status\":\"error\",\"request_id\":\"" + req.id + "\",\"error\":\"create_failed\"}";
                    }
                }
            } else if (op == "file_read") {
                std::string token = extract_json_string(req.raw, "token");
                std::string path = extract_json_string(req.raw, "path");
                void* sessptr = nullptr;
                int gr = get_session_by_token(instance_, token.c_str(), &sessptr);
                if (gr != 0) {
                    resp = "{\"status\":\"error\",\"request_id\":\"" + req.id + "\",\"error\":\"invalid_session\"}";
                } else {
                    char* buf = nullptr; size_t sz = 0;
                    int rr = file_read(instance_, sessptr, path.c_str(), &buf, &sz);
                    delete reinterpret_cast<SessionInfo*>(sessptr);
                    if (rr == 0) {
                        std::string s(buf, sz);
                        std::string esc;
                        for (char c : s) {
                            if (c == '\\') esc += "\\\\";
                            else if (c == '"') esc += "\\\"";
                            else if (c == '\n') esc += "\\n";
                            else esc += c;
                        }
                        resp = "{\"status\":\"success\",\"request_id\":\"" + req.id + "\",\"data\":\"" + esc + "\"}";
                        delete [] buf;
                    } else {
                        resp = "{\"status\":\"error\",\"request_id\":\"" + req.id + "\",\"error\":\"read_failed\"}";
                    }
                }
            } else if (op == "file_delete") {
                std::string token = extract_json_string(req.raw, "token");
                std::string path = extract_json_string(req.raw, "path");
                void* sessptr = nullptr;
                int gr = get_session_by_token(instance_, token.c_str(), &sessptr);
                if (gr != 0) {
                    resp = "{\"status\":\"error\",\"request_id\":\"" + req.id + "\",\"error\":\"invalid_session\"}";
                } else {
                    int dr = file_delete(instance_, sessptr, path.c_str());
                    delete reinterpret_cast<SessionInfo*>(sessptr);
                    if (dr == 0) resp = "{\"status\":\"success\",\"request_id\":\"" + req.id + "\"}";
                    else resp = "{\"status\":\"error\",\"request_id\":\"" + req.id + "\",\"error\":\"delete_failed\"}";
                }
            } else if (op == "dir_create") {
                std::string token = extract_json_string(req.raw, "token");
                std::string path = extract_json_string(req.raw, "path");
                void* sessptr = nullptr;
                int gr = get_session_by_token(instance_, token.c_str(), &sessptr);
                if (gr != 0) {
                    resp = "{\"status\":\"error\",\"request_id\":\"" + req.id + "\",\"error\":\"invalid_session\"}";
                } else {
                    int dc = dir_create(instance_, sessptr, path.c_str());
                    delete reinterpret_cast<SessionInfo*>(sessptr);
                    if (dc == 0) resp = "{\"status\":\"success\",\"request_id\":\"" + req.id + "\"}";
                    else resp = "{\"status\":\"error\",\"request_id\":\"" + req.id + "\",\"error\":\"mkdir_failed\"}";
                }
            } else if (op == "dir_list") {
                std::string token = extract_json_string(req.raw, "token");
                std::string path = extract_json_string(req.raw, "path");
                void* sessptr = nullptr;
                int gr = get_session_by_token(instance_, token.c_str(), &sessptr);
                if (gr != 0) {
                    resp = "{\"status\":\"error\",\"request_id\":\"" + req.id + "\",\"error\":\"invalid_session\"}";
                } else {
                    FileEntry* entries = nullptr; int cnt = 0;
                    int dl = dir_list(instance_, sessptr, path.c_str(), &entries, &cnt);
                    delete reinterpret_cast<SessionInfo*>(sessptr);
                    if (dl == 0) {
                        std::ostringstream oss;
                        oss << "{\"status\":\"success\",\"request_id\":\"" << req.id << "\",\"entries\":[";
                        for (int i = 0; i < cnt; ++i) {
                            if (i) oss << ",";
                            oss << "{\"name\":\"" << entries[i].name << "\",\"type\":\"" << (int)entries[i].type << "\"}";
                        }
                        oss << "]}";
                        resp = oss.str();
                        if (entries) delete [] entries;
                    } else {
                        resp = "{\"status\":\"error\",\"request_id\":\"" + req.id + "\",\"error\":\"list_failed\"}";
                    }
                }
            } else if (op == "user_create") {
                std::string token = extract_json_string(req.raw, "token");
                std::string username = extract_json_string(req.raw, "username");
                std::string password = extract_json_string(req.raw, "password");
                std::string role_s = extract_json_string(req.raw, "role");
                UserRole role = UserRole::NORMAL;
                if (role_s == "admin" || role_s == "ADMIN" || role_s == "1") role = UserRole::ADMIN;
                void* admin_sess = nullptr;
                if (!token.empty()) {
                    int gr = get_session_by_token(instance_, token.c_str(), &admin_sess);
                    if (gr != 0) admin_sess = nullptr;
                }
                int uc = user_create(instance_, admin_sess, username.c_str(), password.c_str(), role);
                if (admin_sess) delete reinterpret_cast<SessionInfo*>(admin_sess);
                if (uc == 0) resp = "{\"status\":\"success\",\"request_id\":\"" + req.id + "\"}";
                else resp = "{\"status\":\"error\",\"request_id\":\"" + req.id + "\",\"error\":\"create_failed\"}";
            } else {
                resp = "{\"status\":\"error\",\"request_id\":\"" + req.id + "\",\"error\":\"unknown_operation\"}";
            }
        }

        if (req.is_http && req.pending) {
            Pending* p = reinterpret_cast<Pending*>(req.pending);
            {
                std::lock_guard<std::mutex> lg(p->m);
                p->response = resp;
                p->done = true;
            }
            p->cv.notify_one();
        } else {
            std::string out = resp + "\n";
            send(req.client_fd, out.c_str(), out.size(), 0);
        }
    }
}
