#!/usr/bin/env python3
import struct
import sys

path = sys.argv[1] if len(sys.argv)>1 else 'test_student.omni'
with open(path,'rb') as f:
    hdr = f.read(512)
    if len(hdr)<512:
        print('File too small')
        sys.exit(1)
    magic = hdr[0:8].decode('ascii',errors='ignore')
    format_version = struct.unpack_from('<I', hdr, 8)[0]
    total_size = struct.unpack_from('<Q', hdr, 12)[0]
    header_size = struct.unpack_from('<Q', hdr, 20)[0]
    block_size = struct.unpack_from('<Q', hdr, 28)[0]
    # offsets according to odf_types.hpp layout
    user_table_offset = struct.unpack_from('<I', hdr, 156)[0]
    max_users = struct.unpack_from('<I', hdr, 160)[0]
    file_state_offset = struct.unpack_from('<I', hdr, 164)[0]
    change_log_offset = struct.unpack_from('<I', hdr, 168)[0]

    print('magic:',magic)
    print('format_version:',hex(format_version))
    print('total_size',total_size,'header_size',header_size,'block_size',block_size)
    print('user_table_offset',user_table_offset,'max_users',max_users)
    print('file_state_offset',file_state_offset,'change_log_offset',change_log_offset)

    f.seek(user_table_offset)
    users = []
    for i in range(max_users):
        data = f.read(128)
        if not data or len(data)<128:
            break
        username = data[0:32].split(b'\x00',1)[0].decode('utf-8',errors='ignore')
        password_hash = data[32:96].split(b'\x00',1)[0].hex()
        role = struct.unpack_from('<I', data, 96)[0]
        created_time = struct.unpack_from('<Q', data, 100)[0]
        last_login = struct.unpack_from('<Q', data, 108)[0]
        is_active = data[116]
        if is_active:
            users.append((i,username,password_hash,role,created_time,last_login))
    print('active users:',len(users))
    for u in users:
        print('slot',u[0],'username',u[1],'role',u[3],'created',u[4],'last_login',u[5],'phash_len',len(u[2]))
