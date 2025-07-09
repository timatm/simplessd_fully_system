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


int main() {
    printf("nvme_interface_test\n");
    int err = 0;
    err = init_device(); 
    if (err == COMMAND_FAILD) {
        printf("Failed to initialize device\n");
        return 1;
    }
    err = ims_init();
    if (err == COMMAND_FAILD) {
        printf("Failed to initialize IMS\n");
        return 1;
    }
    err = monitor_IMS(DUMP_LBNPOOL_INFO);
    if (err == COMMAND_FAILD) {
        printf("Failed to Monitor IMS\n");
        return 1;
    }
    sstable_info info("0001",1,2,20);

    void* raw_ptr = nullptr;
    if (posix_memalign(&raw_ptr, 4096, DB_BLOCK_SIZE)) {
        perror("posix_memalign failed");
        exit(1);
    }
    // memset(raw_ptr, 0xAB, DB_BLOCK_SIZE);
    // err = nvme_write_sstable(info,(char*)raw_ptr);
    // if (err == COMMAND_FAILD) {
    //     printf("Failed to Write SStable\n");
    // }
    memset(raw_ptr, 0, DB_BLOCK_SIZE);
    err = monitor_IMS(DUMP_MAPPING_INFO);
    err = nvme_read_sstable(info.filename, (char*)raw_ptr);
    if (err == COMMAND_FAILD) {
        printf("Failed to Read SStable\n");
        return 1;
    }
    
    err = ims_close();
    close_device();
    return 0;
}



// int main(){
//     printf("nvme_interface_test\n");
//     nmc_config_t *config  = new nmc_config_t();
//     init_nmc_config(config);
//     int err = 0;
//     const char *dev_path = "/dev/nvme0n1";
//     int fd = open(dev_path, O_RDWR);
//     if (fd < 0) {
//         perror("open nvme device");
//         return 1;
//     }
//     err = ims_init(config,fd);
//     // config.data_len = 4096; // data length
//     // void *data = aligned_alloc(4096, config.data_len);
//     // if (!data) {
//     //     perror("alloc");
//     //     close(fd);
//     //     return 1;
//     // }
//     // memset(data, 0, config.data_len);
//     // config.data = (char *)data;
//     // config.metadata_len = 0;
//     // config.metadata =  NULL;
//     // err = ims_nvme_write(config, fd);
//     // config.data_len = 4096; // data length
//     // void *data = aligned_alloc(4096, config.data_len);
//     // if (!data) {
//     //     perror("alloc");
//     //     close(fd);
//     //     return 1;
//     // }
//     // memset(data, 0, config.data_len);
//     // config.data = (char *)data;
//     // config.metadata_len = 0;
//     // config.metadata =  NULL;
//     // err = ims_nvme_write(config, fd);
//     if(err == 0){
//         printf("write success\n");
//     }
//     else{
//         printf("write failed\n");
//         printf("error code: %d\n", err);
//     }
//     close(fd);
//     return 0;
// }