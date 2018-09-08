#ifndef CONFIG_H__
#define CONFIG_H__
extern "C" {
#include <relic/relic.h>
}
namespace emp {
const static int AES_BATCH_SIZE = 2048;
const static int HASH_BUFFER_SIZE = 1024*8;
const static int NETWORK_BUFFER_SIZE = 1024*16;//Should change depending on the network
const static int FILE_BUFFER_SIZE = 1024*16;
const static int CHECK_BUFFER_SIZE = 1024*8;

const static int XOR = -1;
const static int PUBLIC = 0;
const static int ALICE = 1;
const static int BOB = 2;

namespace gTbl {
static eb_t tbl[RELIC_EB_TABLE_MAX];
void init(); // implement in "emp-tool/utils/constants.cpp"
}

#if defined(unix) || defined(__unix__) || defined(__unix) || defined(__APPLE__)
#define UNIX_PLATFORM
#endif
}
#endif// CONFIG_H__
