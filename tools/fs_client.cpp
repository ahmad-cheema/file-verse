#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cout << "Usage: fs_client <host> <port>" << std::endl;
        return 1;
    }
    const char* host = argv[1];
    int port = std::atoi(argv[2]);

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return 1; }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    inet_pton(AF_INET, host, &sa.sin_addr);
    if (connect(s, (struct sockaddr*)&sa, sizeof(sa)) < 0) { perror("connect"); return 1; }

    std::string line;
    while (std::getline(std::cin, line)) {
        // send line with newline
        line.push_back('\n');
        send(s, line.c_str(), line.size(), 0);
        // receive response (simple blocking read)
        char buf[4096];
        ssize_t r = recv(s, buf, sizeof(buf) - 1, 0);
        if (r <= 0) break;
        buf[r] = '\0';
        std::cout << buf << std::endl;
    }
    close(s);
    return 0;
}
