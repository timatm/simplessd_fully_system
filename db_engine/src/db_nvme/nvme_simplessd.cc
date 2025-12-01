
extern "C" {
#include <libnvme.h>
}
#include <fcntl.h>
#include "debug.hh"
#include "nvme_simplessd.hh"
#include <stdio.h>
#include <unistd.h>
#include <limits.h>

#include <cstring> 
#include <string>
#include <array>
#include <cstdint>
int nvme_fd;


int gem5Driver::pass_io_command(nmc_config_t *config){
    int err;
    config->PSDT      = 0;  /* use PRP */
    config->PRP1      = (uintptr_t)config->data;
    config->NSID      = 1;
    pr_debug("==== check config ====");
    pr_debug("data = %p", config->data);
    pr_debug("data_len = %u", config->data_len);
    pr_debug("metadata = %p", config->metadata);
    pr_debug("metadata_len = %u", config->metadata_len);
    pr_debug("fd             : %d", nvme_fd);
    pr_debug("OPCODE         : 0x%x", config->OPCODE);
    pr_debug("flags          : 0x%x", config->flags);
    pr_debug("PSDT           : 0x%x", config->PSDT );
    pr_debug("rsvd           : 0x%x", config->rsvd);
    pr_debug("NSID           : 0x%x", config->NSID);
    pr_debug("cdw02          : %u", config->cdw02);
    pr_debug("cdw03          : %u", config->cdw03);
    pr_debug("cdw10          : 0x%x", config->cdw10);
    pr_debug("cdw11          : 0x%x", config->cdw11);
    pr_debug("cdw12          : 0x%x", config->cdw12);
    pr_debug("cdw13          : 0x%x", config->cdw13);
    pr_debug("cdw14          : 0x%x", config->cdw14);
    pr_debug("cdw15          : 0x%x", config->cdw15);
    pr_debug("data_len       : 0x%x", config->data_len);
    pr_debug("data           : %p", config->data);
    pr_debug("metadata_len   : 0x%x", config->metadata_len);
    pr_debug("metadata       : %p", config->metadata);
    pr_debug("timeout_ms     : %d", config->timeout_ms);
    pr_debug("&result        : %p", config->result);
    pr_debug("===========================");
    if(config->dry){
        return err;
    }
    err = nvme_io_passthru(nvme_fd, config->OPCODE, config->flags, config->rsvd, config->NSID,
    config->cdw02, config->cdw03, config->cdw10, config->cdw11, config->cdw12,
    config->cdw13, config->cdw14, config->cdw15, config->data_len, config->data,
    config->metadata_len, config->metadata, config->timeout_ms, &config->result);
    if(err == 0){
        pr_debug("nvme command pass success");
    }
    else{
        pr_error("nvme command pass failed");
        pr_error("error code: 0x%x", err);
    }
    return err;
}

void gem5Driver::fill_filename_to_dwords(const std::string& filename, uint32_t* dwords_out) {
    std::array<uint8_t, 20> raw = {0}; 
    std::memcpy(raw.data(), filename.data(), std::min(filename.size(), raw.size()));

    for (int i = 0; i < 5; ++i) {
        dwords_out[i] = 
            (static_cast<uint32_t>(raw[i*4 + 3]) << 24 ) |
            (static_cast<uint32_t>(raw[i*4 + 2]) << 16 ) |
            (static_cast<uint32_t>(raw[i*4 + 1]) << 8  ) |
            (static_cast<uint32_t>(raw[i*4 + 0]) << 0  ) ;
    }
}
void gem5Driver::fill_uint64_to_dwords(uint64_t input, uint32_t* dwords_out) {
    uint32_t low  = (uint32_t)input;
    uint32_t high = (uint32_t)(input >> 32);
    dwords_out[0] = low;
    dwords_out[1] = high;
}

int gem5Driver::nvme_ims_init(){
    int err = 0;
    nmc_config_t config_obj;
    nmc_config_t *config = &config_obj;
    init_nmc_config(config); 
    config->OPCODE = OPCODE_IMS_INIT;
    config->PSDT      = 0;
    // config->PRP1      = (uintptr_t)nullptr;
    err = pass_io_command(config);
    if(err == STATUS_OPERATION_SUCCESS){
        err = COMMAND_SUCCESS;
    }
    else{
        pr_error("Init IMS failed");
        pr_error("error code: 0x%x", err);
        err = COMMAND_FAILED;
    }
    return err;
}

int gem5Driver::nvme_ims_close(){
    int err = 0;
    nmc_config_t config_obj;
    nmc_config_t *config = &config_obj;
    init_nmc_config(config); 
    config->OPCODE = OPCODE_IMS_CLOSE;
    config->PSDT      = 0;
    // config->PRP1      = (uintptr_t)nullptr;
    err = pass_io_command(config);
    if(err == STATUS_OPERATION_SUCCESS){
        err = COMMAND_SUCCESS;
    }
    else{
        pr_error("Close IMS failed");
        pr_error("error code: 0x%x", err);
        err = COMMAND_FAILED;
    }
    return err;
}

int gem5Driver::nvme_monitor_IMS(int monitor_type){
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
            pr_error("Monitor does't have this subcommand :%d",monitor_type);
            return COMMAND_FAILED;
            break;
    }
    err = pass_io_command(config);
    if(err == STATUS_OPERATION_SUCCESS){
        err = COMMAND_SUCCESS;
    }
    else{
        pr_error("Monitor IMS failed");
        pr_error("error code: 0x%x", err);
        err = COMMAND_FAILED;
    }
    return err;
}

int gem5Driver::nvme_write_sstable(sstable_info info,char *buffer){
    
    if(buffer == nullptr){
        pr_error("Write sstable failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    if(info.filename.size() == 0){
        pr_error("Write sstable failed ,file name is empty");
        return COMMAND_FAILED;
    }
    int err = 0;
    hostInfo req(info.filename,info.level,info.min,info.max);
    std::string enc_hostinfo = req.encode();
    err = nvme_write_metadata(enc_hostinfo.data(),enc_hostinfo.size());
    if(err != OPERATION_SUCCESS){
        pr_error("Write hostInfo metadata failed");
        return COMMAND_FAILED;
    }
    nmc_config_t config_obj;
    nmc_config_t *config = &config_obj;
    init_nmc_config(config); 

    // config->dry = true;
    config->OPCODE = OPCODE_WRITE_SSTABLE;
    config->data = buffer;
    config->data_len = DB_BLOCK_SIZE;
    // TODO
    // config->cdw02 = info.min;
    // config->cdw03 = info.max;
    // config->cdw10 = info.level;
    // uint32_t filename_dwords[5] = {0};
    // fill_filename_to_dwords(info.filename,filename_dwords);
    // config->cdw11 = filename_dwords[0];
    // config->cdw12 = filename_dwords[1];
    // config->cdw13 = filename_dwords[2];
    // config->cdw14 = filename_dwords[3];
    // config->cdw15 = filename_dwords[4];
    // pr_debug("start nvme write");
    // pr_debug("=============== SStable INFO ===============");
    // pr_debug("Filename: %s to uint 0x%x 0x%x 0x%x 0x%x 0x%x",info.filename.c_str(),config->cdw11,config->cdw12,config->cdw13,config->cdw14,config->cdw15);
    // pr_debug("Level:%u  | Range[%u ~ %u]",config->cdw10,config->cdw02,config->cdw03);
    // pr_debug("=============================================");
    err = pass_io_command(config);
    if(err == STATUS_OPERATION_SUCCESS){
        pr_debug("nvme write success");
        err = COMMAND_SUCCESS;
    }
    else{
        pr_error("nvme write SStable failed");
        pr_error("error code: 0x%x", err);
        err = COMMAND_FAILED;
    }
    return err;
}

int gem5Driver::nvme_write_log(uint64_t lpn,char *buffer){
    
    if(buffer == nullptr){
        pr_error("Write log failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err;
    nmc_config_t config_obj;
    nmc_config_t *config = &config_obj;
    init_nmc_config(config); 
    
    // config->dry = true;
    config->OPCODE = OPCODE_WRITE_LOG;
    config->data = buffer;
    config->data_len = DB_PAGE_SIZE;
    uint32_t lpn_dword[2] = {0};
    fill_uint64_to_dwords(lpn,lpn_dword);
    config->cdw02 = lpn_dword[0];
    config->cdw03 = lpn_dword[1];
    err = pass_io_command(config);
    if(err == STATUS_OPERATION_SUCCESS){
        pr_debug("nvme write success");
        err = COMMAND_SUCCESS;
    }
    else{
        pr_error("nvme write log failed");
        pr_error("error code: 0x%x", err);
        err = COMMAND_FAILED;
    }
    return err;
}


int gem5Driver::nvme_write_block(uint32_t lbn,char *buffer){
    
    if(buffer == nullptr){
        pr_error("Write blcok failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err;
    nmc_config_t config_obj;
    nmc_config_t *config = &config_obj;
    init_nmc_config(config); 
    
    // config->dry = true;
    config->OPCODE = OPCODE_WRITE_BLOCK;
    config->data = buffer;
    config->data_len = DB_BLOCK_SIZE;
    config->cdw12 = lbn;
    err = pass_io_command(config);
    if(err == STATUS_OPERATION_SUCCESS){
        pr_debug("nvme write block success");
        err = COMMAND_SUCCESS;
    }
    else{
        pr_error("nvme write block failed");
        pr_error("error code: 0x%x", err);
        err = COMMAND_FAILED;
    }
    return err;
}



int gem5Driver::nvme_write_metadata(char *buffer,size_t size){

    if(buffer == nullptr){
        pr_error("Write metadata failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err;
    nmc_config_t config_obj;
    nmc_config_t *config = &config_obj;
    init_nmc_config(config); 

    // config->dry = true;
    config->OPCODE = OPCODE_WRITE_BUFFER;
    config->data = buffer;
    config->data_len = size;
    config->cdw12 = size;
    err = pass_io_command(config);
    if(err == STATUS_OPERATION_SUCCESS){
        pr_debug("nvme write success");
        err = COMMAND_SUCCESS;
    }
    else{
        pr_error("nvme write metadata fail");
        pr_error("error code: 0x%x", err);
        err = COMMAND_FAILED;
    }
    return err;
}

int gem5Driver::nvme_read_metadata(char *buffer,size_t size){

    if(buffer == nullptr){
        pr_error("Write metadata failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err;
    nmc_config_t config_obj;
    nmc_config_t *config = &config_obj;
    init_nmc_config(config); 

    // config->dry = true;
    config->OPCODE = OPCODE_READ_BUFFER;
    config->data = buffer;
    config->data_len = size;
    config->cdw12 = size;
    err = pass_io_command(config);
    if(err == STATUS_OPERATION_SUCCESS){
        pr_debug("nvme read success");
        err = COMMAND_SUCCESS;
    }
    else{
        pr_error("nvme read failed");
        pr_error("error code: 0x%x", err);
        err = COMMAND_FAILED;
    }
    return err;
}


int gem5Driver::nvme_read_sstable(std::string filename,char *buffer){
    if(buffer == nullptr){
        pr_error("Read sstable failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err;
    hostInfo req(filename);
    std::string enc_hostinfo = req.encode();
    err = nvme_read_metadata(const_cast<char*>(enc_hostinfo.data()), enc_hostinfo.size());
    nmc_config_t config_obj;
    nmc_config_t *config = &config_obj;
    init_nmc_config(config); 
    config->data_len = DB_BLOCK_SIZE;
    config->data     = buffer;
    config->OPCODE    = OPCODE_READ_SSTABLE;
    config->PSDT      = 0; /* use PRP */
    config->meta_addr = (uintptr_t)NULL;
    config->PRP1      = (uintptr_t)config->data;
    // uint32_t filename_dwords[5] = {0};
    // fill_filename_to_dwords(filename,filename_dwords);
    // config->cdw11 = filename_dwords[0];
    // config->cdw12 = filename_dwords[1];
    // config->cdw13 = filename_dwords[2];
    // config->cdw14 = filename_dwords[3];
    // config->cdw15 = filename_dwords[4];
    err = pass_io_command(config);

    if(err == 0){
        pr_debug("nvme read success");
    }
    else{
        pr_error("nvme read failed");
        pr_error("error code: 0x%x", err);
    }
    return err;
}


int gem5Driver::nvme_read_log(uint64_t lpn,char *buffer){
    if(buffer == nullptr){
        pr_error("Write sstable failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err;
    nmc_config_t config_obj;
    nmc_config_t *config = &config_obj;
    init_nmc_config(config); 
    config->data_len = DB_PAGE_SIZE;
    config->data     = buffer;
    config->OPCODE    = OPCODE_READ_LOG;
    config->PSDT      = 0; /* use PRP */
    config->meta_addr = (uintptr_t)NULL;
    config->PRP1      = (uintptr_t)config->data;
    uint32_t lpn_dwords[2] = {0};
    fill_uint64_to_dwords(lpn,lpn_dwords);
    config->cdw02 = lpn_dwords[0];
    config->cdw03 = lpn_dwords[1];
    // config->dry = true; // set dry run to true for debug
    err = pass_io_command(config);
    
    if(err == 0){
        pr_debug("nvme read success");
    }
    else{
        pr_error("nvme read log failed");
        pr_error("error code: 0x%x", err);
    }
    return err;
}

int gem5Driver::nvme_read_block(uint32_t lbn,char *buffer){
    
    if(buffer == nullptr){
        pr_error("Read blcok failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err;
    nmc_config_t config_obj;
    nmc_config_t *config = &config_obj;
    init_nmc_config(config); 
    
    // config->dry = true;
    config->OPCODE = OPCODE_READ_BLOCK ;
    config->data = buffer;
    config->data_len = DB_BLOCK_SIZE;
    config->cdw12 = lbn;
    err = pass_io_command(config);
    if(err == STATUS_OPERATION_SUCCESS){
        pr_debug("nvme read block success");
        err = COMMAND_SUCCESS;
    }
    else{
        pr_error("nvme read block failed");
        pr_error("error code: 0x%x", err);
        err = COMMAND_FAILED;
    }
    return err;
}


void gem5Driver::init_nmc_config(nmc_config_t *config){
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

int gem5Driver::nvme_allcate_lbn(char *buffer){
    if(buffer == nullptr){
        pr_error("Allcate LBN failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err;
    nmc_config_t config_obj;
    nmc_config_t *config = &config_obj;
    init_nmc_config(config); 
    config->data_len = (uint32_t)sizeof(uint64_t);
    config->data     = buffer;
    config->OPCODE    = OPCODE_ALLOCATE ;
    config->PSDT      = 0; /* use PRP */
    config->meta_addr = (uintptr_t)NULL;
    config->PRP1      = (uintptr_t)config->data;
    err = pass_io_command(config);

    if(err == 0){
        pr_debug("nvme read success");
    }
    else{
        pr_error("nvme read log failed");
        pr_error("error code: 0x%x", err);
    }
    return err;
}

int gem5Driver::init_device(){
    const char *dev_path = "/dev/nvme0n1";
    nvme_fd = open(dev_path, O_RDWR);
    if (nvme_fd < 0) {
        perror("open nvme device");
        return COMMAND_FAILED;
    }
    return COMMAND_SUCCESS;
}
int gem5Driver::close_device(){
    if (nvme_fd >= 0) {
        close(nvme_fd);
        nvme_fd = -1;
    }
    return COMMAND_SUCCESS;
}

int gem5Driver::nvme_open_DB(uint8_t *buffer){
    if (buffer == nullptr) {
        pr_error("Open DB failed: null buffer");
        return OPERATION_FAILURE;
    }
    int err;
    nmc_config_t config_obj;
    nmc_config_t *config = &config_obj;
    init_nmc_config(config);
    void *p = NULL;
    int r = posix_memalign(&p, 4096, sizeof(uint32_t));
    if (r != 0 || p == nullptr) {
        pr_error("Open DB failed: posix_memalign failed (%d)", r);
        free(p);
        return OPERATION_FAILURE;
    }
    config->data_len  = (uint32_t)sizeof(uint32_t);
    config->data      = static_cast<char*>(p);
    config->OPCODE    = OPCODE_OPEN_DB;
    config->PSDT      = 0; /* use PRP */
    config->meta_addr = (uintptr_t)NULL;
    config->PRP1      = (uintptr_t)config->data;
    err = pass_io_command(config);

    if(err == 0){
        uint32_t size = 0;
        std::memcpy(&size, config->data, sizeof(size));
        pr_debug("Next, we need to read the data(size:%u)",static_cast<uint32_t>(size));
        err = nvme_read_metadata(reinterpret_cast<char*>(buffer), size);
        if(err == 0){
            pr_debug("nvme_open_DB success.");
        }
        else{
            pr_error("nvme_open_DB failed");
            pr_error("error code: 0x%x", err);
        }
    }
    else{
        pr_error("nvme_open_DB failed");
        pr_error("error code: 0x%x", err);
    }
    free(p);
    return err;
}

// int nvme_close_DB(uint8_t* buffer,size_t size){
//     if (buffer == nullptr) {
//         pr_error("Open DB failed: null buffer");
//         return OPERATION_FAILURE;
//     }
//     std::cout << "Close DB with buffer size: " <<  std::endl;
//     int err;
//     uint32_t datalen;
//     err = ims.close_DB(buffer,size);
//     return err;
// }

int gem5Driver::nvme_close_DB(uint8_t* buffer,size_t size){

    if(buffer == nullptr){
        pr_error("Write sstable failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err = 0;
    err = nvme_write_metadata(reinterpret_cast<char*>(buffer),size);
    if(err == OPERATION_FAILURE){
        pr_error("nvme_write_metadata fail in nvme_close_DB");
        return OPERATION_FAILURE;
    }
    nmc_config_t config_obj;
    nmc_config_t *config = &config_obj;
    init_nmc_config(config); 

    // config->dry = true;
    config->OPCODE = OPCODE_CLOSE_DB;
    config->data = reinterpret_cast<char*>(buffer);
    config->data_len = size;
    config->cdw12 = size;
    err = pass_io_command(config);
    if(err == STATUS_OPERATION_SUCCESS){
        pr_debug("nvme close success");
        err = COMMAND_SUCCESS;
    }
    else{
        pr_error("nvme close failed");
        pr_error("error code: 0x%x", err);
        err = COMMAND_FAILED;
    }
    return err;
}

int gem5Driver::nvme_erase_sstable(std::string filename){
    if(filename.empty()){
        pr_error("erase SStable failed ,filename is nullptr");
        return COMMAND_FAILED;
    }
    int err;
    hostInfo req(filename);
    std::string enc_hostinfo = req.encode();
    err = nvme_write_metadata(enc_hostinfo.data(), enc_hostinfo.size());
    if(err != OPERATION_SUCCESS){
        pr_error("Write hostInfo metadata failed");
        return COMMAND_FAILED;
    }
    nmc_config_t config_obj;
    nmc_config_t *config = &config_obj;
    init_nmc_config(config);
    // config->dry = true;
    config->OPCODE = OPCODE_ERASE_SSTABLE;
    err = pass_io_command(config);
    if(err == STATUS_OPERATION_SUCCESS){
        pr_debug("nvme erase sstable success");
        err = COMMAND_SUCCESS;
    }
    else{
        pr_error("nvme erase sstable failed");
        pr_error("error code: 0x%x", err);
        err = COMMAND_FAILED;
    }
    return err;
}

int gem5Driver::nvme_dump_ims(){
    int err;
    return err;
    
}
int gem5Driver::nvme_read_ssKeyRange(std::string, char* buffer){
    int err;
    return err;
}


int gem5Driver::nvme_search(char* buffer,size_t size){
    if(buffer == nullptr){
        pr_error("nvme_search failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err = 0;
    int err = 0;
    err = nvme_write_metadata(reinterpret_cast<char*>(buffer),size);
    if(err == OPERATION_FAILURE){
        pr_error("nvme_write_metadata fail in nvme_search");
        return OPERATION_FAILURE;
    }
    nmc_config_t config_obj;
    nmc_config_t *config = &config_obj;
    init_nmc_config(config); 

    // config->dry = true;
    config->OPCODE = OPCODE_SEARCH;
    config->data = reinterpret_cast<char*>(buffer);
    config->data_len = size;
    config->cdw12 = size;
    err = pass_io_command(config);
    if(err == STATUS_OPERATION_SUCCESS){
        pr_debug("nvme_search success");
        err = COMMAND_SUCCESS;
    }
    else{
        pr_error("nvme close failed");
        pr_error("error code: 0x%x", err);
        err = COMMAND_FAILED;
    }
    return err;
}