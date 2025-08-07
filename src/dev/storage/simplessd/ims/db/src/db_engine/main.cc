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
// int main() {
//     API db;
//     Status err = db.open();
//     if(err.ok()) {
//         db.dump_all();
//         std::cout << "Database opened successfully.\n";
//     } else {
//         std::cerr << "Failed to open database: " << err.ToString() << "\n";
//         return -1;
//     }
    
//     for (int i = 0; i < 1024; ++i) {
//         std::string key = "key" + std::to_string(i);
//         std::string value = "value" + std::to_string(i);
//         Status s = db.put(key, value);
//         if (!s.ok()) {
//             std::cerr << "Put failed at index " << i << " with key: " << key << "\n";
//         }
//     }
//     db.getSSTable()->waitAllTasksDone();
//     db.getLogManager()->flush_buffer();
//     // db.nvme_->nvme_dump_ims();
//     // for (int i = 0; i < 129; ++i) {
//     //     std::string key = "key2";
//     //     std::string value = "value" + std::to_string(i);
//     //     Status s = db.put(key, value);
//     //     if (!s.ok()) {
//     //         std::cerr << "Put failed at index " << i << " with key: " << key << "\n";
//     //     }
//     // }



//     // std::cout << "Inserted 129 key-value pairs successfully.\n";
//     // db.getSSTable()->waitAllTasksDone();
//     // db.dump_memtable();
//     // db.nvme_->nvme_dump_ims();


//     // db.dump_lsmtree();
//     // char * buffer = (char *)allocateAligned(BLOCK_SIZE);



//     std::string search_value;
//     std::optional<Record> result = db.getLogManager()->readLog(640,0);
//     if(result.has_value()){
//         result->Dump();
//     } else {
//         std::cout << "No record found at LPN 640, offset 0\n";
//     }
//     // db.get("key20", search_value);
//     // std::cout << search_value << std::endl;


//    // // free(buffer);
//     // return 0;
 
// }



int main() {
    API db;
    Status err = db.open();
    if(err.ok()) {
        db.dump_all();
        std::cout << "Database opened successfully.\n";
    } else {
        std::cerr << "Failed to open database: " << err.ToString() << "\n";
        return -1;
    }
    
    for (int i = 0; i < 700; ++i) {
        std::string key = "key" + std::to_string(i);
        std::string value = "value" + std::to_string(i);
        Status s = db.put(key, value);
        if (!s.ok()) {
            std::cerr << "Put failed at index " << i << " with key: " << key << "\n";
        }
    }
   
    db.getSSTable()->waitAllTasksDone();
    // db.getLogManager()->flush_buffer();


    std::string search_value;
    db.get("key500", search_value);
    // std::optional<Record> result = db.getLogManager()->readLog(640,0);
    // if(result.has_value()){
    //     result->Dump();
    // } else {
    //     std::cout << "No record found at LPN 640, offset 0\n";
    // }
    std::cout << "Search result for key0: " << search_value << std::endl;
 
}
