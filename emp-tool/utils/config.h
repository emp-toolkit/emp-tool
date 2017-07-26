#ifndef CONFIG_H__
#define CONFIG_H__

const static int AES_BATCH_SIZE = 2048;
const static int HASH_BUFFER_SIZE = 1024*8;
const static int NETWORK_BUFFER_SIZE = 1024*16;//Should change depending on the network
const static int FILE_BUFFER_SIZE = 1024*16;
const static int CHECK_BUFFER_SIZE = 1024*8;
//#define THREADING

#endif// CONFIG_H__
