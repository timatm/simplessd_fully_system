#ifndef __DISK_H__
#define __DISK_H__
#include <string>
#include <unordered_map>
#include <cstdint>
#include "def.hh"


class Disk {
public:
    void open(const std::string& filename);
    void close();
    int read(uint64_t lpn, uint8_t * buffer);
    int write(uint64_t lpn, const uint8_t * buffer);
    int writeBlock(uint64_t lbn,uint8_t *buffer);
    int readBlock(uint64_t lbn,uint8_t *buffer);
    FILE* file = nullptr;
};
#endif // __DISK_H__
extern Disk disk;
// #define CHANNEL_NUM 8
// #define PACKAGE_NUM 4
// #define DIE_NUM 2
// #define PLANE_NUM 2
// #define BLOCK_NUM 64
// #define PAGE_NUM 128
// #define PAGE_SIZE 16384