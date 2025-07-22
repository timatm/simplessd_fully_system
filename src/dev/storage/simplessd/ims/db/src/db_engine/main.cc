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

int main() {
    API api;

    std::string key1("apple");
    std::string value1("red");
    std::string key2("banana");
    std::string value2("yellow");

    Status s = api.put(key1, value1);
    if (!s.ok()) {
        std::cerr << "Put failed: " << s.ToString() << std::endl;
    }
    s = api.put(key2, value2);
    if (!s.ok()) {
        std::cerr << "Put failed: " << s.ToString() << std::endl;
    }

    

    return 0;
}



// int main() {
//     printf("nvme_interface_test\n");
//     int err = 0;
//     err = init_device(); 
//     if (err == COMMAND_FAILD) {
//         printf("Failed to initialize device\n");
//         return 1;
//     }
//     err = ims_init();
//     if (err == COMMAND_FAILD) {
//         printf("Failed to initialize IMS\n");
//         return 1;
//     }
//     // err = monitor_IMS(DUMP_LBNPOOL_INFO);
//     // if (err == COMMAND_FAILD) {
//     //     printf("Failed to Monitor IMS\n");
//     //     return 1;
//     // }
    
//     // sstable_info info("0002",1,22,40);

//     // void* raw_ptr = nullptr;
//     // if (posix_memalign(&raw_ptr, 4096, DB_BLOCK_SIZE)) {
//     //     perror("posix_memalign failed");
//     //     exit(1);
//     // }

//     void* buffer = nullptr; 
//     if (posix_memalign(&buffer, 4096, DB_PAGE_SIZE)) {
//         perror("posix_memalign failed");
//         exit(1);
//     }
//     memset(buffer, 0xDE, DB_PAGE_SIZE);
//     err = write_log(256, (char*)buffer);
//     // memset(buffer, 0x00, DB_PAGE_SIZE);
//     // err = read_log(400, (char*)buffer);
//     // err = read_log(400,(char*)buffer);
//     // err = allcate_lbn((char*)buffer);
//     // for (int i = 0; i < DB_PAGE_SIZE;i++){
//     //     if( ((uint8_t *)buffer)[i] != 0xDE){
//     //         printf("Read log failed in %d , expect : 0xAB ,real : 0x%x",i,((uint8_t *)buffer)[i]);
//     //         break;
//     //     }
//     // }
//     // memset(raw_ptr, 0xAB, DB_BLOCK_SIZE);
//     // err = nvme_write_sstable(info,(char*)raw_ptr);
//     // if (err == COMMAND_FAILD) {
//     //     printf("Failed to Write SStable\n");
//     // }
//     // memset(raw_ptr, 0, DB_BLOCK_SIZE);
//     // err = nvme_read_sstable(info.filename, (char*)raw_ptr);
//     // if (err == COMMAND_FAILD) {
//     //     printf("Failed to Read SStable\n");
//     //     return 1;
//     // }
    
//     // err = ims_close();
//     close_device();
//     return 0;
// }



