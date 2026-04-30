#ifndef EMP_CONFIG_H__
#define EMP_CONFIG_H__
namespace emp {
const static int AES_BATCH_SIZE = 8;
const static int NETWORK_BUFFER_SIZE2 = 1024*32;
const static int NETWORK_BUFFER_SIZE = 1024*1024;
const static int FILE_BUFFER_SIZE = 1024*16;
const static int CHECK_BUFFER_SIZE = 1024*8;

const static int XOR = -1;
const static int PUBLIC = 0;
const static int ALICE = 1;
const static int BOB = 2;

// Bristol-format gate-type tags (the 4th int per gate record).
const static int AND_GATE = 0;
const static int XOR_GATE = 1;
const static int NOT_GATE = 2;

extern const char fix_key[17];
}
#endif// CONFIG_H__
