
// extern "C" {
// #include <libnvme.h>
// }
#include <fcntl.h>
#include "debug.hh"
#include "nvme_test.hh"

#include <stdio.h>
#include <unistd.h>
#include <limits.h>

#include <cstring> 
#include <string>
#include <array>
#include <cstdint>



// int MyNVMeDriver::nvme_ims_init() {
//     int err = ims.init_IMS();
//     return err;
// }

// int MyNVMeDriver::nvme_ims_close(){
//     int err = 0;
//     err = ims.close_IMS();
//     return err;
// }

int MyNVMeDriver::nvme_write_sstable(sstable_info info,char *buffer){
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
    // pr_debug("[HOST] sstable_info filename=%s level=%d", 
    //          info.filename.c_str(), info.level);
    // pr_debug("[HOST] rangeMin (string) = '%s'", info.min.toString().c_str());
    // pr_debug("[HOST] rangeMax (string) = '%s'", info.max.toString().c_str());
    // pr_debug("[HOST] rangeMin (hex):");
    // info.min.dumpUint();
    // pr_debug("[HOST] rangeMax (hex):");
    // info.max.dumpUint();

    std::string enc_hostinfo = req.encode();
    err = ims.write_meta(reinterpret_cast<uint8_t*>(enc_hostinfo.data()), enc_hostinfo.size());
    if(err != OPERATION_SUCCESS){
        pr_error("Write hostInfo metadata failed");
        return COMMAND_FAILED;
    }
    uint64_t lbn = INVALIDLBN;
    if(info.isCompaction){
        err = ims.write_sstable(lbn,true);
    }
    else{
        err = ims.write_sstable(lbn,false);
    }
    
    if(err != OPERATION_SUCCESS){
        pr_error("Write sstable is fail");
        return err;
    }
    pr_debug("write SStable LBN:%d",lbn);
    err = ims.write_block(lbn,reinterpret_cast<uint8_t*>(buffer));
    return err;
}
int MyNVMeDriver::nvme_write_log(uint64_t lpn,char *buffer){
    // pr_info("Write log to LPN: %lu", lpn);
    if(buffer == nullptr){
        pr_error("Write sstable failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    
    int err = 0;
    err = ims.write_log(lpn,reinterpret_cast<uint8_t*>(buffer));
    return err;
}


int MyNVMeDriver::nvme_read_sstable(std::string filename,char *buffer){
    if(buffer == nullptr){
        pr_error("Read sstable failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err;
    hostInfo req(filename);
    std::string enc_hostinfo = req.encode();
    err = ims.write_meta(reinterpret_cast<uint8_t*>(const_cast<char*>(enc_hostinfo.data())), enc_hostinfo.size());
    if(err != OPERATION_SUCCESS){
        pr_error("Write hostInfo metadata failed");
        return COMMAND_FAILED;
    }
    uint64_t lbn = INVALIDLBN;
    err = ims.read_sstable(lbn);
    if(err != OPERATION_SUCCESS){
        pr_error("Read sstable is fail");
        return err;
    }
    err = ims.read_block(lbn,reinterpret_cast<uint8_t*>(buffer));
    return err;
}


int MyNVMeDriver::nvme_read_log(uint64_t lpn,char *buffer){
    if(buffer == nullptr){
        pr_error("Read log failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    if(lpn >= LPN_NUM){
        pr_error("Read log failed ,LPN is out of limit");
        return COMMAND_FAILED;
    }
    int err;
    err = ims.read_log(lpn,reinterpret_cast<uint8_t*>(buffer));
    return err;
}

int MyNVMeDriver::nvme_erase_sstable(std::string filename){
    if(filename.empty()){
        pr_error("Write metadata failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err;
    hostInfo req(filename);
    std::string enc_hostinfo = req.encode();
    err = ims.write_meta(reinterpret_cast<uint8_t*>(const_cast<char*>(enc_hostinfo.data())), enc_hostinfo.size());
    uint64_t lbn = INVALID_64;
    err = ims.erase_sstable(lbn);
    return err;
}

int MyNVMeDriver::nvme_dump_ims(){
    int err;
    err = ims.dump_IMS();
    return err;
}

int MyNVMeDriver::nvme_allcate_lbn(char *buffer){
    if(buffer == nullptr){
        pr_error("Allcate LBN failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err;
    err = ims.allocate_block(reinterpret_cast<uint64_t*>(buffer));
    return err;
}

int MyNVMeDriver::nvme_open_DB(uint32_t& datalen){
    int err;
    err = ims.open_DB(&datalen);
    if(err == OPERATION_FAILURE){
        pr_error("IMS open DB fail");
        return err;
    }
    return err;
}
int MyNVMeDriver::nvme_close_DB(uint8_t* buffer,size_t size){
    if (buffer == nullptr) {
        pr_error("Open DB failed: null buffer");
        return OPERATION_FAILURE;
    }
    // std::cout << "Close DB with buffer size: " <<  std::endl;
    int err;
    uint32_t datalen;
    err = ims.close_DB(buffer,size);
    return err;
}

int MyNVMeDriver::nvme_read_ssKeyRange(std::string filename, char* buffer){
    if(buffer == nullptr){
        pr_error("nvme_read_ssKeyRange failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    if(filename.empty()){
        pr_error("nvme_read_ssKeyRange failed ,filename is nullptr");
        return COMMAND_FAILED;
    }
    int err;
    hostInfo req(filename);
    std::string enc_hostinfo = req.encode();
    err = ims.write_meta(reinterpret_cast<uint8_t*>(const_cast<char*>(enc_hostinfo.data())), enc_hostinfo.size());
    if(err != OPERATION_SUCCESS){
        pr_error("Write hostInfo metadata failed");
        return COMMAND_FAILED;
    }
    uint64_t lpn = INVALID_64;
    err = ims.read_ssKeyRange(lpn);
    if(lpn == INVALID_64){
        pr_error("read ssKeyRange translate lpn failed");
        return COMMAND_FAILED;
    }
    err = ims.read_log( lpn,reinterpret_cast<uint8_t*>(buffer) );
    return err;
}



int MyNVMeDriver::nvme_write_metadata(char *buffer,size_t size){
    if(buffer == nullptr){
        pr_error("Write metadata failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err = ims.write_meta(reinterpret_cast<uint8_t*>(buffer),size);
    if(err == STATUS_OPERATION_SUCCESS){
        pr_debug("nvme write success");
        err = COMMAND_SUCCESS;
    }
    else{
        pr_error("nvme write matadata fail");
        pr_error("error code: 0x%x", err);
        err = COMMAND_FAILED;
    }
    return err;
}

int MyNVMeDriver::nvme_read_metadata(char *buffer,size_t size){
    if(buffer == nullptr){
        pr_error("Write metadata failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    int err = ims.read_meta(reinterpret_cast<uint8_t*>(buffer),size);
    if(err == STATUS_OPERATION_SUCCESS){
        pr_debug("nvme write success");
        err = COMMAND_SUCCESS;
    }
    else{
        pr_error("nvme read failed");
        pr_error("error code: 0x%x", err);
        err = COMMAND_FAILED;
    }
    return err;
}


int MyNVMeDriver::nvme_write_block(uint32_t lbn, char* buffer){
    int err = OPERATION_FAILURE;
    if(buffer == nullptr){
        pr_error("Write block failed ,data buffer is nullptr");
        return err;
    }
    if(lbn >= LBN_NUM){
        pr_error("Write block failed ,LBN is out of limit");
        return err;
    }
    err = ims.write_block(lbn,reinterpret_cast<uint8_t*>(buffer));
    return err;
}


int MyNVMeDriver::nvme_read_block(uint32_t lbn, char* buffer){
    int err = OPERATION_FAILURE;
    if(buffer == nullptr){
        pr_error("Read block failed ,data buffer is nullptr");
        return err;
    }
    if(lbn >= LBN_NUM){
        pr_error("Read block failed ,LBN is out of limit");
        return err;
    }
    err = ims.read_block(lbn,reinterpret_cast<uint8_t*>(buffer));
    return err;
}
int MyNVMeDriver::nvme_search(char *buffer,size_t size){
    int err = OPERATION_FAILURE;
    if(buffer == nullptr){
        pr_error("nvme_search failed ,data buffer is nullptr");
        return err;
    }
    err = ims.write_meta(reinterpret_cast<uint8_t*>(buffer), size);
    if(err != OPERATION_SUCCESS){
        pr_error("Write hostInfo metadata failed");
        return COMMAND_FAILED;
    }
    std::vector<uint64_t> lbn_list;
    err = ims.search(lbn_list);
    if(err == OPERATION_FAILURE){
        pr_error("nvme_search fail");
    }
    return err;
}

int MyNVMeDriver::nvme_compaction_io(const CompactionIOSimMeta& meta){
    int err = OPERATION_FAILURE;
    std::string buffer = meta.encode();
    err = ims.write_meta(reinterpret_cast<uint8_t*>(buffer.data()), buffer.size());
    if(err != OPERATION_SUCCESS){
        pr_error("Write hostInfo metadata failed");
        return COMMAND_FAILED;
    }
    std::vector<uint64_t> lbn_list;
    err = ims.simulate_compaction_io(lbn_list);
    
    if(err == OPERATION_FAILURE){
        pr_error("nvme_search fail");
    }
    return err;
}
// int MyNVMeDriver::nvme_set_sstable_info(uint32_t *data_len){

// }
// int MyNVMeDriver::nvme_set_log_info(uint32_t *data_len){

// }

int MyNVMeDriver::nvme_read_sstable_page(std::string filename, uint32_t page_off,uint32_t page_num ,char* buffer){
    if(buffer == nullptr){
        pr_error("nvme_read_sstable_page failed ,data buffer is nullptr");
        return COMMAND_FAILED;
    }
    if(filename.empty()){
        pr_error("nvme_read_sstable_page failed ,filename is nullptr");
        return COMMAND_FAILED;
    }
    if (page_num == 0){
        pr_error("nvme_read_sstable_page failed ,page_nun is zero");
        return COMMAND_FAILED;
    }
    if(page_off > IMS_PAGE_NUM || (page_off + page_num) > IMS_PAGE_NUM ){
        pr_error("nvme_read_sstable_page failed ,out of SStable area");
        return COMMAND_FAILED;
    }
    int err;
    hostInfo req(filename);
    std::string enc_hostinfo = req.encode();
    err = ims.write_meta(reinterpret_cast<uint8_t*>(const_cast<char*>(enc_hostinfo.data())), enc_hostinfo.size());
    if(err != OPERATION_SUCCESS){
        pr_error("Write hostInfo metadata failed");
        return COMMAND_FAILED;
    }
    uint64_t lpn = INVALID_64;
    err = ims.read_ssPage(lpn);
    if(lpn == INVALID_64){
        pr_error("nvme_read_sstable_pagetranslate lpn failed");
        return COMMAND_FAILED;
    }
    uint64_t read_page = lpn + static_cast<uint64_t>(page_off);
    for(uint64_t i = 0;i < page_num;i++){
        err = ims.read_log( read_page+i,reinterpret_cast<uint8_t*>(buffer + i*IMS_PAGE_SIZE));
        if(err != OPERATION_SUCCESS){
            pr_error("nvme_read_sstable_page in SStable(%s) failed",filename.c_str());
            return COMMAND_FAILED;
        }
    }
    return OPERATION_SUCCESS;
}

int MyNVMeDriver::nvme_trival_move(sstable_info info){
    if(info.filename.size() == 0){
        pr_error("Trival move failed ,file name is empty");
        return COMMAND_FAILED;
    }
    int err = 0;
    hostInfo req(info.filename,info.level,info.min,info.max);
    // pr_debug("[HOST] sstable_info filename=%s level=%d", 
    //          info.filename.c_str(), info.level);
    // pr_debug("[HOST] rangeMin (string) = '%s'", info.min.toString().c_str());
    // pr_debug("[HOST] rangeMax (string) = '%s'", info.max.toString().c_str());
    // pr_debug("[HOST] rangeMin (hex):");
    // info.min.dumpUint();
    // pr_debug("[HOST] rangeMax (hex):");
    // info.max.dumpUint();

    std::string enc_hostinfo = req.encode();
    err = ims.write_meta(reinterpret_cast<uint8_t*>(enc_hostinfo.data()), enc_hostinfo.size());
    if(err != OPERATION_SUCCESS){
        pr_error("Write hostInfo metadata failed in Trival move");
        return COMMAND_FAILED;
    }
    err = ims.trivial_move();
    if(err != OPERATION_SUCCESS){
        pr_error("Trival move is fail");
        return err;
    }
    return err;
}