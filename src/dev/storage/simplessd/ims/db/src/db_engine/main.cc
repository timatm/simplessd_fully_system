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
// static void* allocateAligned(size_t size) {
//     void* ptr = nullptr;
//     if (posix_memalign(&ptr, 4096, size) != 0 || ptr == nullptr) {
//         throw std::bad_alloc();
//     }
//     std::memset(ptr, 0, size);
//     return ptr;
// }
int main() {
    // API db;
    // Status err = db.open();
    // if(err.ok()) {
    //     db.dump_all();
    //     std::cout << "Database opened successfully.\n";
    // } else {
    //     std::cerr << "Failed to open database: " << err.ToString() << "\n";
    //     return -1;
    // }
    
    // for (int i = 0; i < 1024; ++i) {
    //     std::string key = "key" + std::to_string(i);
    //     std::string value = "value" + std::to_string(i);
    //     Status s = db.put(key, value);
    //     if (!s.ok()) {
    //         std::cerr << "Put failed at index " << i << " with key: " << key << "\n";
    //     }
    // }
    // db.getSSTable()->waitAllTasksDone();
    // db.nvme_->nvme_dump_ims();
    // for (int i = 0; i < 129; ++i) {
    //     std::string key = "key2";
    //     std::string value = "value" + std::to_string(i);
    //     Status s = db.put(key, value);
    //     if (!s.ok()) {
    //         std::cerr << "Put failed at index " << i << " with key: " << key << "\n";
    //     }
    // }



    // std::cout << "Inserted 129 key-value pairs successfully.\n";
    // db.getSSTable()->waitAllTasksDone();
    // db.dump_memtable();
    // db.nvme_->nvme_dump_ims();


    // db.dump_lsmtree();
    // char * buffer = (char *)allocateAligned(BLOCK_SIZE);



    // db.getSSTable()->waitAllTasksDone();
    // std::string search_value;
    // db.get("key131", search_value);
    // std::cout << search_value << std::endl;


    // // free(buffer);
    // return 0;
    API db;
    Status err = db.open();
    // ① 純記憶體單元測試 —— 不經 NVMe、也不經 writeLog / readLog
    Record src(InternalKey("user-key", /*lpn*/640, /*off*/0, /*seq*/1,
                        ValueType::kTypeValue),
            "hello world");
    std::string blob = src.Encode();
    Record dst      = Record::Decode(blob);
    dst.Dump();
    assert(src.internal_key_size == dst.internal_key_size);
    assert(src.value_size        == dst.value_size);
    assert(src.internal_key.info.seq  == dst.internal_key.info.seq);
    assert(src.value == dst.value);
    db.getLogManager()->writeLog(src);                 // 用剛剛的 src Record
    db.getLogManager()->flush_buffer();           // 直接 flush buffer
    auto recOpt = db.getLogManager()->readLog(640, /*offset*/0);
    assert(recOpt);                   // 若為 nullopt 直接掛
    Record dstDisk = *recOpt;
    dstDisk.Dump();
    assert(src.value == dstDisk.value);  // 完整比較
}


