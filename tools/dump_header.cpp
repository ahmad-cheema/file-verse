#include <iostream>
#include <fstream>
#include "../source/include/odf_types.hpp"

int main(int argc, char** argv) {
    const char* path = (argc>1)?argv[1]:"test_student.omni";
    std::ifstream fs(path, std::ios::in | std::ios::binary);
    if (!fs.is_open()) { std::cerr<<"open failed"<<std::endl; return 1; }
    OMNIHeader h;
    fs.read(reinterpret_cast<char*>(&h), sizeof(h));
    std::cout<<"magic='"<<std::string(h.magic,8)<<"'\n";
    std::cout<<"format_version="<<std::hex<<h.format_version<<std::dec<<"\n";
    std::cout<<"total_size="<<h.total_size<<" header_size="<<h.header_size<<" block_size="<<h.block_size<<"\n";
    std::cout<<"user_table_offset="<<h.user_table_offset<<" max_users="<<h.max_users<<"\n";
    std::cout<<"file_state_storage_offset="<<h.file_state_storage_offset<<" change_log_offset="<<h.change_log_offset<<"\n";
    return 0;
}
