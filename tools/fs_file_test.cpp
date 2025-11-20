#include <iostream>
#include <cstring>
#include "../source/include/omni_core.hpp"

int main(int argc, char** argv) {
    const char* omni = "test_student.omni";
    if (argc > 1) omni = argv[1];

    void* inst = nullptr;
    int r = fs_init(&inst, omni, nullptr);
    if (r != 0) {
        std::cerr << "fs_init failed: " << r << std::endl;
        return 1;
    }
    std::cout << "fs_init OK\n";

    // create a directory
    r = dir_create(inst, nullptr, "/docs");
    std::cout << "dir_create /docs => " << r << "\n";

    // create a file
    const char* text = "Hello OFS!";
    r = file_create(inst, nullptr, "/docs/hello.txt", text, strlen(text));
    std::cout << "file_create /docs/hello.txt => " << r << "\n";

    // check exists
    r = file_exists(inst, nullptr, "/docs/hello.txt");
    std::cout << "file_exists => " << r << "\n";

    // read file
    char* buf = nullptr;
    size_t sz = 0;
    r = file_read(inst, nullptr, "/docs/hello.txt", &buf, &sz);
    std::cout << "file_read => " << r << ", size=" << sz << "\n";
    if (r == 0 && buf) {
        std::cout << "content: '" << std::string(buf, sz) << "'\n";
        delete [] buf;
    }

    // list root
    FileEntry* entries = nullptr;
    int count = 0;
    r = dir_list(inst, nullptr, "/", &entries, &count);
    std::cout << "dir_list / => " << r << ", count=" << count << "\n";
    if (entries) {
        for (int i = 0; i < count; ++i) {
            std::cout << " - " << entries[i].name << " (type=" << (int)entries[i].type << ")\n";
        }
        delete [] entries;
    }

    // delete file
    r = file_delete(inst, nullptr, "/docs/hello.txt");
    std::cout << "file_delete => " << r << "\n";

    fs_shutdown(inst);
    std::cout << "fs_shutdown done\n";
    return 0;
}
