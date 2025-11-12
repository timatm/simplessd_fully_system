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
    int readPage(uint64_t lpn, uint8_t * buffer);
    int writePage(uint64_t lpn, const uint8_t * buffer);
    int writeBlock(uint64_t lbn,uint8_t *buffer);
    int readBlock(uint64_t lbn,uint8_t *buffer);
    FILE* file_ = nullptr;
};
#endif // __DISK_H__
