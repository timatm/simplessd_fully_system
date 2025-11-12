#include "disk.hh"
#include "print.hh"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include "def.hh"
void Disk::open(const std::string& filename) {
    file_ = std::fopen(filename.c_str(), "r+b");
    if (!file_) {
        file_ = std::fopen(filename.c_str(), "w+b");
        if (!file_) {
            throw std::runtime_error("Failed to open or create file: " + filename);
        }
    }

    uint64_t expectedSize = static_cast<uint64_t>(LBN_NUM) * static_cast<uint64_t>(LBN_SIZE);


    std::fseek(file_, 0, SEEK_END);
    uint64_t actualSize = std::ftell(file_);

    if (actualSize < expectedSize) {
        std::fseek(file_, expectedSize - 1, SEEK_SET);
        uint8_t zero = 0;
        std::fwrite(&zero, 1, 1, file_);
             
        std::fflush(file_);                            
        pr_info("Disk file expanded to %lu bytes", expectedSize);
    }
    pr_info("Disk file expanded to %lu bytes", expectedSize);
    std::rewind(file_);
    pr_info("Total LBN num: %lu", LBN_NUM);
    pr_info("Open disk success");
}


void Disk::close() {
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

int Disk::readPage(uint64_t lpn, uint8_t * buffer) {
    if (!file_) throw std::runtime_error("Disk not opened.");

    uint64_t offset = lpn * IMS_PAGE_SIZE;
    if (std::fseek(file_, offset, SEEK_SET) != 0) {
        throw std::runtime_error("Seek failed.");
        return -1;
    }

    size_t readBytes = std::fread(buffer, 1, IMS_PAGE_SIZE, file_);
    if (readBytes != IMS_PAGE_SIZE) {
        throw std::runtime_error("Read error or EOF.");
        return -1;
    }
    return 0;
}

int Disk::writePage(uint64_t lpn,const uint8_t * buffer) {
    if (!file_) throw std::runtime_error("Disk not opened.");
    // pr_info("Write LPN[%d] to disk",lpn);
    uint64_t offset = lpn * IMS_PAGE_SIZE;
    if (std::fseek(file_, offset, SEEK_SET) != 0) {
        throw std::runtime_error("Seek failed.");
        return -1;
    }
    // std::cout << "Write offset is " << offset << std::endl;
    size_t writtenBytes = std::fwrite(buffer, 1, IMS_PAGE_SIZE, file_);
    if (writtenBytes != IMS_PAGE_SIZE) {
        std::cerr << "[ERROR] fwrite failed: only wrote " << writtenBytes << " bytes, expected " << IMS_PAGE_SIZE << std::endl;
        perror("fwrite");
        throw std::runtime_error("Write error.");
    }
    // pr_info("Write page at LPN: %lu is success", lpn);
    std::fflush(file_);
    return 0;
}

int Disk::writeBlock(uint64_t lbn ,uint8_t *buffer) {
    uint16_t ret = 0;
    uint64_t lpn = LBN2LPN(lbn);
    if (file_) {
        for(int i = 0;i < IMS_PAGE_NUM;i++){
            uint8_t *page_ptr = buffer + i * IMS_PAGE_SIZE;
            // assert(reinterpret_cast<uintptr_t>(page_ptr) % 4096 == 0 && "buffer must be 4KB aligned");

            // pr_debug("Write block at page: %d LPN: %lu", i, lpn);
            uint16_t written = writePage(lpn+i, page_ptr);
            if (written != 0) {
                pr_error("write block failed at page: %d LPN: %lu", i, lpn);
            }
            ret += written;
        }
    }

    return ret;
}

int Disk::readBlock(uint64_t lbn ,uint8_t *buffer) {
    uint16_t ret = 0;
    uint64_t lpn = LBN2LPN(lbn);
    if (file_) {
        for (int i = 0; i < IMS_PAGE_NUM; i++) {
        // pr_debug("Read block at page: %d LPN: %lu", i, lpn);
        uint8_t *page_ptr = buffer + i * IMS_PAGE_SIZE;

        int read_result = readPage(lpn+i, page_ptr);
        if (read_result != 0) {
            pr_error("read block failed at page: %d LPN: %lu", i, lpn);
            break;
        }
        ret += read_result;
        }
    }

  return ret;
}



