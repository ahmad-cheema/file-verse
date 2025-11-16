#include "../source/server/fifo_server.hpp"
#include <iostream>
#include <csignal>
#include "../source/include/omni_core.hpp"

static FIFOService* g_service = nullptr;

void sigint_handler(int) {
    if (g_service) g_service->stop();
    exit(0);
}

int main(int argc, char** argv) {
    int port = 8080;
    if (argc > 1) port = std::atoi(argv[1]);

    // Require FILEVERSE_OMNI env var and instance
    const char* omni = std::getenv("FILEVERSE_OMNI");
    if (!omni) {
        std::cerr << "Please set FILEVERSE_OMNI to the .omni path before starting the server" << std::endl;
        return 1;
    }

    void* inst_ptr = nullptr;
    int r = fs_init(&inst_ptr, omni, nullptr);
    if (r != 0) {
        std::cerr << "fs_init failed: " << r << std::endl;
        return 1;
    }
    OFSInstance* inst = reinterpret_cast<OFSInstance*>(inst_ptr);

    FIFOService service(port, inst);
    g_service = &service;
    signal(SIGINT, sigint_handler);

    if (!service.start()) {
        std::cerr << "Service failed to start" << std::endl;
        fs_shutdown(inst_ptr);
        return 1;
    }

    std::cout << "FIFO server running on port " << port << " (ctrl-c to stop)" << std::endl;
    // block main thread until stopped
    while (true) std::this_thread::sleep_for(std::chrono::seconds(60));

    fs_shutdown(inst_ptr);
    return 0;
}
