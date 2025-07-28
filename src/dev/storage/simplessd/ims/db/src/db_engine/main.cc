#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include "../db_nvme/../db_nvme/nvme_interface.hh"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "main.hh"
#include <iomanip>   
#include <cstdint>    
#include "db_api.hh"
static void* allocateAligned(size_t size) {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, 4096, size) != 0 || ptr == nullptr) {
        throw std::bad_alloc();
    }
    std::memset(ptr, 0, size);
    return ptr;
}
int main() {
    API db;
    for (int i = 0; i < 129; ++i) {
        std::string key = "key" + std::to_string(i);
        std::string value = "value" + std::to_string(i);
        Status s = db.put(key, value);
        if (!s.ok()) {
            std::cerr << "Put failed at index " << i << " with key: " << key << "\n";
        }
    }
    // for (int i = 0; i < 129; ++i) {
    //     std::string key = "key2";
    //     std::string value = "value" + std::to_string(i);
    //     Status s = db.put(key, value);
    //     if (!s.ok()) {
    //         std::cerr << "Put failed at index " << i << " with key: " << key << "\n";
    //     }
    // }
    std::cout << "Inserted 129 key-value pairs successfully.\n";
    db.getSSTable()->waitAllTasksDone();
    db.dump_memtable();
    db.dump_lsmtree();
    
    return 0;
}


