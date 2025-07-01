
extern "C" {
#include <libnvme.h>
}
#include <fcntl.h>
#include "debug.hh"
#include "nvme_interface.hh"
#include <stdio.h>
#include <unistd.h>
#include <limits.h>

int nvme_fd = -1;

int pass_io_command(nmc_config_t *config){
    int err;
    config->meta_addr = (uintptr_t)NULL;
    config->PSDT      = 0;  /* use PRP */
    config->PRP1      = (uintptr_t)config->data;
    config->NSID      = 1;
    pr("==== check config ====");
    pr("data = %p", config->data);
    pr("data_len = %u", config->data_len);
    pr("metadata = %p", config->metadata);
    pr("metadata_len = %u", config->metadata_len);
    pr("fd             : %d", config->fd);
    pr("OPCODE         : 0x%x", config->OPCODE);
    pr("flags          : 0x%x", config->flags);
    pr("rsvd           : 0x%x", config->rsvd);
    pr("NSID           : 0x%x", config->NSID);
    pr("cdw02          : 0x%x", config->cdw02);
    pr("cdw03          : 0x%x", config->cdw03);
    pr("cdw10          : 0x%x", config->cdw10);
    pr("cdw11          : 0x%x", config->cdw11);
    pr("cdw12          : 0x%x", config->cdw12);
    pr("cdw13          : 0x%x", config->cdw13);
    pr("cdw14          : 0x%x", config->cdw14);
    pr("cdw15          : 0x%x", config->cdw15);
    pr("data_len       : 0x%x", config->data_len);
    pr("data           : %p", config->data);
    pr("metadata_len   : 0x%x", config->metadata_len);
    pr("metadata       : %p", config->metadata);
    pr("timeout_ms     : %d", config->timeout_ms);
    pr("&result        : %p", config->result);
    pr("===========================");
    err = nvme_io_passthru(nvme_fd, config->OPCODE, config->flags, config->rsvd, config->NSID,
    config->cdw02, config->cdw03, config->cdw10, config->cdw11, config->cdw12,
    config->cdw13, config->cdw14, config->cdw15, config->data_len, config->data,
    config->metadata_len, config->metadata, config->timeout_ms, &config->result);
    if(err == 0){
        pr("nvme command pass success");
    }
    else{
        pr("nvme command pass failed");
        pr("error code: 0x%x", err);
    }
    return err;
}

int ims_init(){
    int err = 0;
    nmc_config_t config_obj;
    nmc_config_t *config = &config_obj;
    init_nmc_config(config); 
    config->OPCODE = OPCODE_INIT_IMS;
    config->PSDT      = 0;
    // config->PRP1      = (uintptr_t)nullptr;
    err = pass_io_command(config);
    if(err == STATUS_OPERATION_SUCCESS){
        pr("Init IMS success");
        err = COMMAND_SUCCESS;
    }
    else{
        pr("Init IMS failed");
        pr("error code: 0x%x", err);
        err = COMMAND_FAILD;
    }
    return err;
}

int monitor_IMS(int monitor_type){
    int err;
    nmc_config_t config_obj;
    nmc_config_t *config = &config_obj;
    init_nmc_config(config); 
    config->OPCODE    = OPCODE_MONITOR_IMS;
    config->PSDT      = 0;
    switch(monitor_type){
        case DUMP_MAPPING_INFO:
            config->cdw13 = DUMP_MAPPING_INFO;
            break;
        case DUMP_LBNPOOL_INFO:
            config->cdw13 = DUMP_LBNPOOL_INFO;
            break;
        default:
            pr("Monitor does't have this subcommand :%d",monitor_type);
            return COMMAND_FAILD;
            break;
    }
    err = pass_io_command(config);
    if(err == STATUS_OPERATION_SUCCESS){
        pr("Monitor IMS success");
        err = COMMAND_SUCCESS;
    }
    else{
        pr("Monitor IMS failed");
        pr("error code: 0x%x", err);
        err = COMMAND_FAILD;
    }
    return err;
}

int ims_nvme_write(char *buffer){
    int err;
    nmc_config_t config_obj;
    nmc_config_t *config = &config_obj;
    init_nmc_config(config); 
    config->data_len = BLOCK_SIZE;
    config->OPCODE    = OPCODE_WRITE_SSTABLE;
    config->PRP1      = (uintptr_t)buffer;
    pr("start nvme write");
    err = pass_io_command(config);
    if(err == STATUS_OPERATION_SUCCESS){
        pr("nvme write success");
        err = COMMAND_SUCCESS;
    }
    else{
        pr("nvme write failed");
        pr("error code: %d", err);
        err = COMMAND_FAILD;
    }
    return err;
}


int ims_nvme_read(nmc_config_t *config){
    int err;
    config->data_len = PAGE_SIZE;
    config->data     = (char*)aligned_alloc(getpagesize(), config->data_len);
    assert_return(config->data, errno, "failed to allocate data buffer...");
    config->OPCODE    = OPCODE_READ_SSTABLE;
    config->PSDT      = 0; /* use PRP */
    config->meta_addr = (uintptr_t)NULL;
    config->PRP1      = (uintptr_t)config->data;
    if (config->dry)
    {
        printf("dev          : ");
        pr("opcode       : 0x%02x", config->OPCODE);
        pr("nsid         : 0x%02x", config->NSID);
        pr("cdw2         : 0x%08x", config->cdw02);
        pr("cdw3         : 0x%08x", config->cdw02);
        pr("data_addr    : %p", config->data);
        pr("madata_addr  : %p", config->metadata);
        pr("data_len     : 0x%08x", config->data_len);
        pr("mdata_len    : 0x%08x", config->metadata_len);
        pr("slba         : 0x%08lx", config->slba);
        pr("nlb          : 0x%08x", config->nlb);
        pr("cdw10        : 0x%08x", config->cdw10);
        pr("cdw11        : 0x%08x", config->cdw11);
        pr("cdw12        : 0x%08x", config->cdw12);
        pr("cdw13        : 0x%08x", config->cdw13);
        pr("cdw14        : 0x%08x", config->cdw14);
        pr("cdw15        : 0x%08x", config->cdw15);
    }
    err = nvme_io_passthru(config->fd, config->OPCODE, config->flags, config->rsvd, config->NSID,
    config->cdw02, config->cdw03, config->cdw10, config->cdw11, config->cdw12,
    config->cdw13, config->cdw14, config->cdw15, config->data_len, config->data,
    config->metadata_len, config->metadata, config->timeout_ms, &config->result);

    if(err == 0){
        pr("nvme write success");
    }
    else{
        pr("nvme write failed");
        pr("error code: %d", err);
    }
    return err;
}

void init_nmc_config(nmc_config_t *config){
    config->OPCODE = 0;
    config->dry = 0;
    config->data_file = NULL;
    config->flags = 0;
    config->rsvd = 0;
    config->result = 0;
    config->timeout_ms = 10000; // default timeout
    config->data = NULL;
    config->data_len = 0;
    config->metadata = NULL;
    config->metadata_len = 0;
    config->NSID = 0;
    config->slba = 0;
    config->nlb = 0;    
    config->cdw02 = 0;
    config->cdw03 = 0;
    config->cdw10 = 0;
    config->cdw11 = 0;
    config->cdw12 = 0;
    config->cdw13 = 0;
    config->cdw14 = 0;
    config->cdw15 = 0;
    return;
}

// void print_fd_target(int fd) {
//     char path[64];
//     char resolved_path[64];

//     snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
//     ssize_t len = readlink(path, resolved_path, sizeof(resolved_path) - 1);
//     if (len != -1) {
//         resolved_path[len] = '\0';
//         printf("%s\n", fd, resolved_path);
//     } else {
//         perror("readlink");
//     }
// }

int init_device(){
    const char *dev_path = "/dev/nvme0n1";
    nvme_fd = open(dev_path, O_RDWR);
    if (nvme_fd < 0) {
        perror("open nvme device");
        return COMMAND_FAILD;
    }
    return COMMAND_SUCCESS;
}
int close_device(){
    if (nvme_fd >= 0) {
        close(nvme_fd);
        nvme_fd = -1;
    }
    return COMMAND_SUCCESS;
}