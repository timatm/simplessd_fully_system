#include <cstdlib>
#include <iostream>


#include "IMS_interface.hh"
#include "persistence.hh"
#include "mapping_table.hh"
#include "print.hh"
#include "tree.hh"
int Persistence::readMappingTable(uint64_t lpn,uint8_t *buffer,size_t size) {
    int err;
    if (buffer == nullptr) {
        pr_error("[ERROR] Memory allocation failed.");
        return OPERATION_FAILURE;
    }
    if (size != IMS_PAGE_SIZE){
        pr_error("Memory allocation failed.");
        return OPERATION_FAILURE;
    }
    err = pDisk_->readPage(lpn, buffer);
    if(err){
        return OPERATION_FAILURE;
    }
    return OPERATION_SUCCESS;
}

int Persistence::flushMappingTable(const std::unordered_map<std::string, uint64_t>& mappingTable) {
    uint64_t mappingPageLBN = sp_ptr_old_->mapping_store;
    pr_info("Flush mapping table to disk at LBN: %lu", mappingPageLBN);

    uint64_t lpn = LBN2LPN(mappingPageLBN);
    auto* buffer = (uint8_t*)malloc(IMS_PAGE_SIZE);
    if (!buffer) return OPERATION_FAILURE;

    memset(buffer, 0xFF, IMS_PAGE_SIZE);
    auto* page = reinterpret_cast<mappingTablePerPage*>(buffer);
    if (!page) return OPERATION_FAILURE;

    int err = OPERATION_FAILURE;
    size_t idx = 0;
    sp_ptr_new_->mapping_page_num = 0;

    for (const auto& [filename, lbn] : mappingTable) {
        if (idx == MAPPING_TABLE_ENTRIES) {
            page->entry_num = idx;
            err = pDisk_->writePage(lpn++, buffer);
            if (err == OPERATION_FAILURE) {
                return OPERATION_FAILURE;
            }
            pr_info("Flushed mapping table page with %zu entries", idx);
            sp_ptr_new_->mapping_page_num++;
            memset(buffer, 0xFF, IMS_PAGE_SIZE);
            idx = 0;
        }

        mappingEntry& entry = page->entry[idx++];
        std::memset(&entry, 0, sizeof(entry));
        std::strncpy(entry.fileName, filename.c_str(),sizeof(entry.fileName) - 1);
        entry.fileName[sizeof(entry.fileName) - 1] = '\0';
        entry.lbn = lbn;
        auto node = tree_.find_node(filename);
        if (node) {
            entry.level = node->levelInfo;
            entry.channel = node->channelInfo;
            entry.minRange = node->rangeMin;
            entry.maxRange = node->rangeMax;
        }
    }

    if (idx > 0) {
        page->entry_num = idx;
        err = pDisk_->writePage(lpn, buffer);
        if (err == OPERATION_SUCCESS) {
            pr_info("Flushed final mapping page with %zu entries", idx);
            sp_ptr_new_->mapping_page_num++;
        }
    }
    pr_info("Flushed mapping done ,mapping store in LBN: %lu (num of pages: %lu)",mappingPageLBN ,sp_ptr_new_->mapping_page_num );
    free(buffer);
    return err == 0 ? OPERATION_SUCCESS : OPERATION_FAILURE;
}



int  Persistence::readBlock(uint64_t lbn,uint8_t *buffer,size_t size){
    int err;
    if (buffer == nullptr) {
        pr_error("[ERROR] Memory allocation failed.");
        return OPERATION_FAILURE;
    }
    if (size != BLOCK_SIZE){
        pr_error("[ERROR] Memory allocation failed.");
        return OPERATION_FAILURE;
    }
    err = pDisk_->readBlock(lbn, buffer);
    if(err){
        return OPERATION_FAILURE;
    }
    return OPERATION_SUCCESS;
}

int Persistence::writeBlock(uint64_t lbn,uint8_t *buffer,size_t size){
    int err;
    if(!ENABLE_DISK){
        return OPERATION_SUCCESS;
    }
    if(!pDisk_->file_){
        pr_error("Disk does't open");
        return OPERATION_FAILURE;
    }
    if (buffer == nullptr) {
        pr_error("[ERROR] Memory allocation failed.");
        return OPERATION_FAILURE;
    }
    if (size != BLOCK_SIZE){
        pr_error("Memory allocation failed.");
        return OPERATION_FAILURE;
    }
    err = pDisk_->writeBlock(lbn, buffer);
    if(!err){
        return err;
    }
    return err;
}

// int Persistence::readSStablePage(uint64_t lpn,uint8_t *buffer,size_t size){
//     int err;
//     if (buffer == nullptr) {
//         std::cerr << "[ERROR] Memory allocation failed.\n";
//         return OPERATION_FAILURE;
//     }
//     if (size != IMS_PAGE_SIZE){
//         pr_debug("[ERROR] Memory allocation failed.");
//         return OPERATION_FAILURE;
//     }
//     err = pDisk_->readPage(lpn,buffer);
//     if(err){
//         return OPERATION_FAILURE;
//     }
//     return OPERATION_SUCCESS;
// }

int Persistence::readPage(uint64_t lpn,uint8_t *buffer,size_t size){
    int err;
    if (pDisk_ == nullptr) {
        pr_error("Disk not initialized.");
        return OPERATION_FAILURE;
    }
    if (buffer == nullptr) {
        pr_error("Memory allocation failed");
        return OPERATION_FAILURE;
    }
    if (size != IMS_PAGE_SIZE){
        pr_error("data size doesn't match");
        return OPERATION_FAILURE;
    }
    err = pDisk_->readPage(lpn,buffer);
    if(err){
        return OPERATION_FAILURE;
    }
    return OPERATION_SUCCESS;
}

int Persistence::writePage(uint64_t lpn,uint8_t *buffer,size_t size){
    int err;
    if (pDisk_ == nullptr) {
        pr_error("Disk not initialized");
        return OPERATION_FAILURE;
    }
    if (buffer == nullptr) {
        pr_error("Memory allocation failed");
        return OPERATION_FAILURE;
    }
    if (size != IMS_PAGE_SIZE){
        pr_error("data size doesn't match");
        return OPERATION_FAILURE;
    }
    err = pDisk_->writePage(lpn,buffer);
    if(err){
        return OPERATION_FAILURE;
    }
    return OPERATION_SUCCESS;
}


int Persistence::eraseBlock(uint64_t lbn){
    int err;
    if(!ENABLE_DISK){
        return OPERATION_SUCCESS;
    }
    if(!pDisk_->file_){
        pr_error("Disk does't open");
        return OPERATION_FAILURE;
    }
    std::string buffer(BLOCK_SIZE, static_cast<char>(0xFF));
    err = pDisk_->writeBlock(lbn, reinterpret_cast<uint8_t *>(buffer.data()));
    if(!err){
        return err;
    }
    return err;
}