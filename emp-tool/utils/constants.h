#ifndef EMP_CONFIG_H__
#define EMP_CONFIG_H__
namespace emp {
const static int AES_BATCH_SIZE = 8;
const static int HASH_BUFFER_SIZE = 1024*8;
const static int NETWORK_BUFFER_SIZE2 = 1024*32;
const static int NETWORK_BUFFER_SIZE = 1024*1024;
const static int FILE_BUFFER_SIZE = 1024*16;
const static int CHECK_BUFFER_SIZE = 1024*8;

const static int XOR = -1;
const static int PUBLIC = 0;
const static int ALICE = 1;
const static int BOB = 2;

}
#endif// CONFIG_H__
