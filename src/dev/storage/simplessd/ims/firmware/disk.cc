#include "disk.hh"
#include "print.hh"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <iostream>
void Disk::open(const std::string& filename) {
    file = std::fopen(filename.c_str(), "r+b");
    if (!file) {
        file = std::fopen(filename.c_str(), "w+b");
        if (!file) {
            throw std::runtime_error("Failed to open or create file: " + filename);
        }
    }

    uint64_t expectedSize = LBN_NUM * IMS_PAGE_SIZE;

    std::fseek(file, 0, SEEK_END);
    uint64_t actualSize = std::ftell(file);

    if (actualSize < expectedSize) {
        std::fseek(file, expectedSize - 1, SEEK_SET);  // 定位到最後一個 byte
        std::fwrite("", 1, 1, file);                   // 寫入 1 byte，強制擴展
        std::fflush(file);                             // 確保 flush 到磁碟
        pr_info("Disk file expanded to %lu bytes", expectedSize);
    }

    std::rewind(file);
    pr_info("Total LBN num: %lu", LBN_NUM);
    pr_info("Open disk success");
}


void Disk::close() {
    if (file) {
        std::fclose(file);
        file = nullptr;
    }
}

int Disk::read(uint64_t lpn, uint8_t * buffer) {
    if (!file) throw std::runtime_error("Disk not opened.");

    uint64_t offset = lpn * IMS_PAGE_SIZE;
    if (std::fseek(file, offset, SEEK_SET) != 0) {
        throw std::runtime_error("Seek failed.");
        return -1;
    }

    size_t readBytes = std::fread(buffer, 1, IMS_PAGE_SIZE, file);
    if (readBytes != IMS_PAGE_SIZE) {
        throw std::runtime_error("Read error or EOF.");
        return -1;
    }
    return 0;
}

int Disk::write(uint64_t lpn,const uint8_t * buffer) {
    if (!file) throw std::runtime_error("Disk not opened.");

    uint64_t offset = lpn * IMS_PAGE_SIZE;
    if (std::fseek(file, offset, SEEK_SET) != 0) {
        throw std::runtime_error("Seek failed.");
        return -1;
    }

    size_t writtenBytes = std::fwrite(buffer, 1, IMS_PAGE_SIZE, file);
    if (writtenBytes != IMS_PAGE_SIZE) {
        throw std::runtime_error("Write error.");
        return -1;
    }
    std::fflush(file);
    return 0;
}

int Disk::writeBlock(uint64_t lbn ,uint8_t *buffer) {
    uint16_t ret = 0;
    uint64_t lpn = LBN2LPN(lbn);
    if (file) {
        for(int i = 0;i < IMS_PAGE_NUM;i++){
            uint8_t *page_ptr = buffer + i * IMS_PAGE_SIZE;
            lpn = lpn + 1;
            // pr_debug("Write block at page: %d LPN: %lu", i, lpn);
            uint16_t written = write(lpn, page_ptr);
            if (written != 0) {
                pr_info("[ERROR] write block failed at page: %d LPN: %lu", i, lpn);
            }
            ret += written;
        }
    }

    return ret;
}

int Disk::readBlock(uint64_t lbn ,uint8_t *buffer) {
    uint16_t ret = 0;
    uint64_t lpn = LBN2LPN(lbn);
    if (file) {
        for (int i = 0; i < IMS_PAGE_NUM; i++) {
        lpn = lpn +1;
        // pr_debug("Read block at page: %d LPN: %lu", i, lpn);
        uint8_t *page_ptr = buffer + i * IMS_PAGE_SIZE;

        int read_result = read(lpn, page_ptr);
        if (read_result != 0) {
            pr_info("[ERROR] read block failed at page: %d LPN: %lu", i, lpn);
            break;
        }
        ret += read_result;
        }
    }

  return ret;
}



