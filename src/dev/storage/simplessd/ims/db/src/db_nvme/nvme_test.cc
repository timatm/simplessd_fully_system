
extern "C" {
#include <libnvme.h>
}
#include <fcntl.h>
#include "debug.hh"
#include "nvme_interface.hh"
#include "IMS_interface.hh"
#include <stdio.h>
#include <unistd.h>
#include <limits.h>

#include <cstring> 
#include <string>
#include <array>
#include <cstdint>

IMS_interface ims;

void fill_filename_to_dwords(const std::string& filename, uint32_t* dwords_out) {
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
void fill_uint64_to_dwords(uint64_t input, uint32_t* dwords_out) {
    uint32_t low  = (uint32_t)input;
    uint32_t high = (uint32_t)(input >> 32);
    dwords_out[0] = low;
    dwords_out[1] = high;
}

int nvme_ims_init(){
    int err = ims.init_IMS();
    return err;
}

int nvme_ims_close(){
    int err = 0;
    err = ims.close_IMS();
    return err;
}

int nvme_write_sstable(sstable_info info,char *buffer){
    if(buffer == nullptr){
        pr("Write sstable failed ,data buffer is nullptr");
        return COMMAND_FAILD;
    }
    int err = 0;
    hostInfo req(info.filename,info.level,info.min,info.max);
    err = ims.write_sstable(&req,reinterpret_cast<uint8_t*>(buffer));
    return err;
}
int nvme_write_log(uint64_t lpn,char *buffer){
    if(buffer == nullptr){
        pr("Write sstable failed ,data buffer is nullptr");
        return COMMAND_FAILD;
    }
    
    int err = 0;
    err = ims.write_log(lpn,reinterpret_cast<uint8_t*>(buffer));
    return err;
}


int nvme_read_sstable(std::string filename,char *buffer){
    if(buffer == nullptr){
        pr("Write sstable failed ,data buffer is nullptr");
        return COMMAND_FAILD;
    }
    int err;
    hostInfo req(filename);
    err = ims.read_sstable(&req,reinterpret_cast<uint8_t*>(buffer));
    return err;
}


int nvme_read_log(uint64_t lpn,char *buffer){
    if(buffer == nullptr){
        pr("Write sstable failed ,data buffer is nullptr");
        return COMMAND_FAILD;
    }
    int err;
    err = ims.read_log(lpn,reinterpret_cast<uint8_t*>(buffer));
    return err;
}



int nvme_allcate_lbn(char *buffer){
    if(buffer == nullptr){
        pr("Allcate LBN failed ,data buffer is nullptr");
        return COMMAND_FAILD;
    }
    int err;
    err = ims.allocate_block(reinterpret_cast<uint64_t*>(buffer));
    return err;
}