#include <iostream>
#include <cstdlib>
#include "omni_core.hpp"
#include "odf_types.hpp"

int main() {
    const char* path = "test_student.omni";
    std::cout << "Formatting: " << path << std::endl;
    int r = fs_format(path, nullptr);
    std::cout << "fs_format => " << r << std::endl;

    void* instance = nullptr;
    r = fs_init(&instance, path, nullptr);
    std::cout << "fs_init => " << r << std::endl;
    if (r != 0) return 1;

    // Create admin user (bootstrap with instance, no admin_session)
    r = user_create(instance, nullptr, "admin", "admin123", UserRole::ADMIN);
    std::cout << "user_create(admin) => " << r << std::endl;

    // Login
    void* session = nullptr;
    r = user_login(instance, &session, "admin", "admin123");
    std::cout << "user_login(admin) => " << r << std::endl;

    if (r == 0 && session) {
        UserInfo* users = nullptr;
        int count = 0;
        r = user_list(instance, session, &users, &count);
        std::cout << "user_list => " << r << ", count=" << count << std::endl;
        for (int i = 0; i < count; ++i) {
            std::cout << " - " << users[i].username << " role=" << static_cast<int>(users[i].role) << "\n";
        }
        delete [] users;
    }

    fs_shutdown(instance);
    std::cout << "Shutdown complete" << std::endl;
    return 0;
}
