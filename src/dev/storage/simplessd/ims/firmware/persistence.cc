#include <cstdlib>
#include <iostream>


#include "IMS_interface.hh"
#include "persistence.hh"
#include "mapping_table.hh"
#include "print.hh"
#include "tree.hh"
int Persistence::readMappingTable(uint64_t lbn,uint8_t *buffer,size_t size) {
    int err;
    if (buffer == nullptr) {
        std::cerr << "[ERROR] Memory allocation failed.\n";
        return OPERATION_FAILURE;
    }
    if (size != IMS_PAGE_SIZE){
        pr_debug("[ERROR] Memory allocation failed.");
        return OPERATION_FAILURE;
    }
    err = disk.read(lbn, buffer);
    if(err){
        return OPERATION_FAILURE;
    }
    return OPERATION_SUCCESS;
}

int Persistence::flushMappingTable(std::unordered_map<std::string, uint64_t>& mappingTable) {

    uint64_t mappingPageLBN = sp_ptr_old->mapping_store;
    pr_info("Flush mapping table to disk at LBN: %lu", mappingPageLBN);
    uint64_t lpn = LBN2LPN(mappingPageLBN);
    uint8_t *buffer = (uint8_t*)malloc(IMS_PAGE_SIZE);
    int err = OPERATION_FAILURE;
    if(!ENABLE_DISK){
        return OPERATION_SUCCESS;
    }
    if (!buffer) {
        std::cerr << "[ERROR] Memory allocation failed.\n";
        return OPERATION_FAILURE;
    }
    memset(buffer, 0xFF, IMS_PAGE_SIZE);

    auto *page = reinterpret_cast<mappingTablePerPage*>(buffer);
    if(!page){
        pr_info("[ERROR] Memory allocation failed.\n");
        return OPERATION_FAILURE;
    }
    size_t idx = 0;
    for (const auto& pair : mappingTable) {
        if (idx == MAPPING_TABLE_ENTRIES){
            page->entry_num = idx;
            err = disk.write(lpn, buffer);
            if(err == OPERATION_FAILURE) {
                free(buffer);
                return OPERATION_FAILURE;
            }
            pr_info("Flushed mapping table to disk at LPN: %lu", lpn);
            pr_info("Mapping store entry num:%d", page->entry_num);
            sp_ptr_new->mapping_page_num++;
            memset(buffer, 0xFF, IMS_PAGE_SIZE);
            lpn++;
            idx = 0;
        }
        mappingEntry &entry = page->entry[idx++];
        memset(entry.fileName, 0, sizeof(entry.fileName));
        strncpy(entry.fileName, pair.first.c_str(), sizeof(entry.fileName) - 1);
        entry.fileName[sizeof(entry.fileName) - 1] = '\0';
        entry.lbn = pair.second;
        auto node = tree.find_node(pair.first.c_str());
        if (!node) {
            pr_debug("Cannot find node for %s", pair.first.c_str());
        }
        else{
            entry.level = node->levelInfo;
            entry.channel = node->channelInfo;
            entry.minRange = node->rangeMin;
            entry.maxRange = node->rangeMax;
        }
    }
    if(idx > 0) {
        sp_ptr_new->mapping_page_num++;
        page->entry_num = idx;
        err = disk.write(lpn, buffer);
        if(err == OPERATION_FAILURE) {
            free(buffer);
            return OPERATION_FAILURE;
        }
        pr_info("Flushed mapping table to disk at LPN: %lu", lpn);
        pr_info("Mapping store entry num:%d", page->entry_num);
    }
    pr_info("Mapping store pages num: %lu", sp_ptr_new->mapping_page_num);
    
    free(buffer);
    return OPERATION_SUCCESS;
}

int  Persistence::readSStable(uint64_t lbn,uint8_t *buffer,size_t size){
    int err;
    if (buffer == nullptr) {
        std::cerr << "[ERROR] Memory allocation failed.\n";
        return OPERATION_FAILURE;
    }
    if (size != BLOCK_SIZE){
        pr_debug("[ERROR] Memory allocation failed.");
        return OPERATION_FAILURE;
    }
    err = disk.readBlock(lbn, buffer);
    if(err){
        return OPERATION_FAILURE;
    }
    return OPERATION_SUCCESS;
}
int Persistence::flushSStable(uint64_t lbn,uint8_t *buffer,size_t size){
    int err;
    if(!ENABLE_DISK){
        return OPERATION_SUCCESS;
    }
    if(!disk.file){
        pr_debug("Disk does't open");
        return OPERATION_FAILURE;
    }
    if (buffer == nullptr) {
        std::cerr << "[ERROR] Memory allocation failed.\n";
        return OPERATION_FAILURE;
    }
    if (size != BLOCK_SIZE){
        pr_debug("[ERROR] Memory allocation failed.");
        return OPERATION_FAILURE;
    }
    err = disk.writeBlock(lbn, buffer);
    if(!err){
        return err;
    }
    return err;
}

int Persistence::readSStablePage(uint64_t lpn,uint8_t *buffer,size_t size){
    int err;
    if (buffer == nullptr) {
        std::cerr << "[ERROR] Memory allocation failed.\n";
        return OPERATION_FAILURE;
    }
    if (size != IMS_PAGE_SIZE){
        pr_debug("[ERROR] Memory allocation failed.");
        return OPERATION_FAILURE;
    }
    err = disk.read(lpn,buffer);
    if(err){
        return OPERATION_FAILURE;
    }
    return OPERATION_SUCCESS;
}

