#include <stdio.h>
#include <stdlib.h>
#include "../db_nvme/../db_nvme/nvme_interface.hh"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "main.hh"

int main() {
    printf("nvme_interface_test\n");

    nmc_config_t config_obj;
    nmc_config_t *config = &config_obj;
    init_nmc_config(config); 

    int err = 0;
    const char *dev_path = "/dev/nvme0n1";
    int fd = open(dev_path, O_RDWR);
    if (fd < 0) {
        perror("open nvme device");
        return 1;
    }
    config->fd = fd;
    err = ims_init(config);
    config->monitor_type = DUMP_LBNPOOL_INFO;
    err = monitor_IMS(config);
    close(fd);
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