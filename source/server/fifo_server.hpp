#ifndef FIFO_SERVER_HPP
#define FIFO_SERVER_HPP

#include <string>
#include <thread>
#include <mutex>
#include <deque>
#include <condition_variable>
#include "../include/omni_core.hpp"

// Simple request/response wrapper
struct FSRequest {
    std::string raw;   // raw JSON request
    int client_fd;     // socket fd to reply to
    std::string id;    // request_id if present
    bool is_http = false;
    void* pending = nullptr; // pointer to Pending when used for HTTP
};

struct FSResponse {
    std::string raw; // raw JSON response
    int client_fd;
};

class FIFOService {
public:
    FIFOService(int port, OFSInstance* inst);
    ~FIFOService();

    // start listening (background thread)
    bool start();

    // stop service
    void stop();

private:
    struct Pending {
        std::mutex m;
        std::condition_variable cv;
        std::string response;
        bool done = false;
    };

    void accept_loop();
    void reader_loop(int client_fd);
    void handle_http_connection(int client_fd);
    void worker_loop();

    int port_;
    int server_fd_ = -1;
    std::thread accept_thread_;
    std::thread worker_thread_;

    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<FSRequest> request_queue_;

    std::mutex resp_mutex_;
    std::deque<FSResponse> response_queue_;

    bool running_ = false;
    OFSInstance* instance_ = nullptr;
};

#endif // FIFO_SERVER_HPP
