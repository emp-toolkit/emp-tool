#ifndef CONFIG_H__
#define CONFIG_H__
//#define THREADING
#define AES_BATCH_SIZE 1024 // may need to change for different cache size, for the best performance.
#define HASH_BUFFER_SIZE 1024*8*8

#define NETWORK_BUFFER_SIZE 1024*8
#define FILE_BUFFER_SIZE 1024*16
#define CHECK_BUFFER_SIZE 1024*8
//#define SERVER_IP  "54.165.226.80"
#define SERVER_IP  "127.0.0.1"
//#define COUNT_IO
#endif// CONFIG_H__
