
extern "C" {
#include <libnvme.h>
}
#include <fcntl.h>
#include "debug.hh"
#include "nvme_test.hh"
#include "IMS_interface.hh"
#include <stdio.h>
#include <unistd.h>
#include <limits.h>

#include <cstring> 
#include <string>
#include <array>
#include <cstdint>



int MyNVMeDriver::nvme_ims_init() {
    int err = ims.init_IMS();
    return err;
}

int MyNVMeDriver::nvme_ims_close(){
    int err = 0;
    err = ims.close_IMS();
    return err;
}

int MyNVMeDriver::nvme_write_sstable(sstable_info info,char *buffer){
    if(buffer == nullptr){
        pr("Write sstable failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err = 0;
    hostInfo req(info.filename,info.level,info.min,info.max);
    err = ims.write_sstable(&req,reinterpret_cast<uint8_t*>(buffer));
    return err;
}
int MyNVMeDriver::nvme_write_log(uint64_t lpn,char *buffer){
    pr_info("Write log to LPN: %lu", lpn);
    if(buffer == nullptr){
        pr("Write sstable failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    
    int err = 0;
    err = ims.write_log(lpn,reinterpret_cast<uint8_t*>(buffer));
    return err;
}


int MyNVMeDriver::nvme_read_sstable(std::string filename,char *buffer){
    if(buffer == nullptr){
        pr("Write sstable failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err;
    hostInfo req(filename);
    err = ims.read_sstable(&req,reinterpret_cast<uint8_t*>(buffer));
    return err;
}


int MyNVMeDriver::nvme_read_log(uint64_t lpn,char *buffer){
    if(buffer == nullptr){
        pr("Read sstable failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err;
    err = ims.read_log(lpn,reinterpret_cast<uint8_t*>(buffer));
    return err;
}

int MyNVMeDriver::nvme_erase_sstable(std::string filename){
    if(filename.empty()){
        pr("Write metadata failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err;
    hostInfo req(filename);
    err = ims.erase_sstable(&req);
    return err;
}

int MyNVMeDriver::nvme_dump_ims(){
    int err;
    err = ims.dump_IMS();
    return err;
}

int MyNVMeDriver::nvme_allcate_lbn(char *buffer){
    if(buffer == nullptr){
        pr("Allcate LBN failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err;
    err = ims.allocate_block(reinterpret_cast<uint64_t*>(buffer));
    return err;
}

int MyNVMeDriver::nvme_open_DB(uint8_t *buffer){
    if (buffer == nullptr) {
        pr("Open DB failed: null buffer");
        return OPERATION_FAILURE;
    }
    int err;
    err = ims.open_DB(buffer, DB_PAGE_SIZE);
    return err;
}
int MyNVMeDriver::nvme_close_DB(){
    std::cout << "Close DB with buffer size: " <<  std::endl;
    return OPERATION_SUCCESS;
}

int MyNVMeDriver::nvme_read_ssKeyRange(std::string filename, char* buffer){
    if(buffer == nullptr){
        pr("Write sstable failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err;
    hostInfo req(filename);
    err = ims.read_sstable(&req,reinterpret_cast<uint8_t*>(buffer));
    return err;
}



int MyNVMeDriver::nvme_write_metadata(uint64_t lpn,char *buffer,size_t size){
    if(buffer == nullptr){
        pr("Write metadata failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err;
    // int err = ims.read_sstable(&req,reinterpret_cast<uint8_t*>(buffer));
    if(err == STATUS_OPERATION_SUCCESS){
        pr("nvme write success");
        err = COMMAND_SUCCESS;
    }
    else{
        pr("nvme write failed");
        pr("error code: 0x%x", err);
        err = COMMAND_FAILED;
    }
    return err;
}

