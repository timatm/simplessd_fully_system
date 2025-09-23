
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
        pr_debug("Write sstable failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    if(info.filename.size() == 0){
        pr_debug("Write sstable failed ,file name is empty");
        return COMMAND_FAILED;
    }
    int err = 0;
    hostInfo req(info.filename,info.level,info.min,info.max);
    std::string enc_hostinfo = req.encode();
    err = ims.write_meta(reinterpret_cast<uint8_t*>(const_cast<char*>(enc_hostinfo.data())), enc_hostinfo.size());
    if(err != OPERATION_SUCCESS){
        pr_debug("Write hostInfo metadata failed");
        return COMMAND_FAILED;
    }
    err = ims.write_sstable(reinterpret_cast<uint8_t*>(buffer));
    return err;
}
int MyNVMeDriver::nvme_write_log(uint64_t lpn,char *buffer){
    // pr_info("Write log to LPN: %lu", lpn);
    if(buffer == nullptr){
        pr_debug("Write sstable failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    
    int err = 0;
    err = ims.write_log(lpn,reinterpret_cast<uint8_t*>(buffer));
    return err;
}


int MyNVMeDriver::nvme_read_sstable(std::string filename,char *buffer){
    if(buffer == nullptr){
        pr_debug("Read sstable failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err;
    hostInfo req(filename);
    std::string enc_hostinfo = req.encode();
    err = ims.write_meta(reinterpret_cast<uint8_t*>(const_cast<char*>(enc_hostinfo.data())), enc_hostinfo.size());
    if(err != OPERATION_SUCCESS){
        pr_debug("Write hostInfo metadata failed");
        return COMMAND_FAILED;
    }
    err = ims.read_sstable(reinterpret_cast<uint8_t*>(buffer));
    return err;
}


int MyNVMeDriver::nvme_read_log(uint64_t lpn,char *buffer){
    if(buffer == nullptr){
        pr_debug("Read log failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    if(lpn >= LPN_NUM){
        pr_debug("Read log failed ,LPN is out of limit");
        return COMMAND_FAILED;
    }
    int err;
    err = ims.read_log(lpn,reinterpret_cast<uint8_t*>(buffer));
    return err;
}

int MyNVMeDriver::nvme_erase_sstable(std::string filename){
    if(filename.empty()){
        pr_debug("Write metadata failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err;
    hostInfo req(filename);
    std::string enc_hostinfo = req.encode();
    err = ims.write_meta(reinterpret_cast<uint8_t*>(const_cast<char*>(enc_hostinfo.data())), enc_hostinfo.size());
    err = ims.erase_sstable();
    return err;
}

int MyNVMeDriver::nvme_dump_ims(){
    int err;
    err = ims.dump_IMS();
    return err;
}

int MyNVMeDriver::nvme_allcate_lbn(char *buffer){
    if(buffer == nullptr){
        pr_debug("Allcate LBN failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err;
    err = ims.allocate_block(reinterpret_cast<uint64_t*>(buffer));
    return err;
}

int MyNVMeDriver::nvme_open_DB(uint8_t *buffer){
    if (buffer == nullptr) {
        pr_debug("Open DB failed: null buffer");
        return OPERATION_FAILURE;
    }
    int err;
    uint32_t datalen;
    err = ims.open_DB(&datalen);
    err = nvme_read_metadata(reinterpret_cast<char*>(buffer), datalen);
    return err;
}
int MyNVMeDriver::nvme_close_DB(uint8_t* buffer,size_t size){
    if (buffer == nullptr) {
        pr_debug("Open DB failed: null buffer");
        return OPERATION_FAILURE;
    }
    std::cout << "Close DB with buffer size: " <<  std::endl;
    int err;
    uint32_t datalen;
    err = ims.close_DB(buffer,size);
    return err;
}

int MyNVMeDriver::nvme_read_ssKeyRange(std::string filename, char* buffer){
    if(buffer == nullptr){
        pr_debug("Write sstable failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err;
    hostInfo req(filename);
    err = ims.read_ssKeyRange(&req,reinterpret_cast<uint8_t*>(buffer));
    return err;
}



int MyNVMeDriver::nvme_write_metadata(char *buffer,size_t size){
    if(buffer == nullptr){
        pr_debug("Write metadata failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err = ims.write_meta(reinterpret_cast<uint8_t*>(buffer),size);
    if(err == STATUS_OPERATION_SUCCESS){
        pr_debug("nvme write success");
        err = COMMAND_SUCCESS;
    }
    else{
        pr_debug("nvme write failed");
        pr_debug("error code: 0x%x", err);
        err = COMMAND_FAILED;
    }
    return err;
}

int MyNVMeDriver::nvme_read_metadata(char *buffer,size_t size){
    if(buffer == nullptr){
        pr_debug("Write metadata failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err = ims.read_meta(reinterpret_cast<uint8_t*>(buffer),size);
    if(err == STATUS_OPERATION_SUCCESS){
        pr_debug("nvme write success");
        err = COMMAND_SUCCESS;
    }
    else{
        pr_debug("nvme write failed");
        pr_debug("error code: 0x%x", err);
        err = COMMAND_FAILED;
    }
    return err;
}


int MyNVMeDriver::nvme_write_block(uint32_t lbn, char* buffer){
    int err = OPERATION_FAILURE;
    if(buffer == nullptr){
        pr_debug("Write block failed ,data buffer is nullptr");
        return err;
    }
    if(lbn >= LBN_NUM){
        pr_debug("Write block failed ,LBN is out of limit");
        return err;
    }
    err = ims.write_block(lbn,reinterpret_cast<uint8_t*>(buffer));
    return err;
}


int MyNVMeDriver::nvme_read_block(uint32_t lbn, char* buffer){
    int err = OPERATION_FAILURE;
    if(buffer == nullptr){
        pr_debug("Read block failed ,data buffer is nullptr");
        return err;
    }
    if(lbn >= LBN_NUM){
        pr_debug("Read block failed ,LBN is out of limit");
        return err;
    }
    err = ims.read_block(lbn,reinterpret_cast<uint8_t*>(buffer));
    return err;
}

// int MyNVMeDriver::nvme_set_sstable_info(uint32_t *data_len){

// }
// int MyNVMeDriver::nvme_set_log_info(uint32_t *data_len){

// }