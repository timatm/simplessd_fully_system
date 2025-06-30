#include "IMS_interface.hh"
#include <algorithm>
#include <numeric>

#include "def.hh"
#include "lbn_pool.hh"
#include "persistence.hh"
#include "tree.hh"
#include "mapping_table.hh"
#include "print.hh"
#include "log.hh"

Tree tree;
Mapping mappingManager(lbnPoolManager);
LBNPool lbnPoolManager; 
Persistence persistenceManager;
Log logManager;
super_page *sp_ptr_old  = new super_page(MAGIC,1,2);
super_page *sp_ptr_new = new super_page(MAGIC,1,2);


int IMS_interface::write_sstable(hostInfo request,uint8_t *buffer){
    int err = OPERATION_FAILURE;
    std::string filename = request.filename;
    int level = request.levelInfo;
    int rangeMin = request.rangeMin;
    int rangeMax = request.rangeMax;
    pr_info("Write request for file: %s, level: %d, range: [%d, %d]", filename.c_str(), level, rangeMin, rangeMax);
    // Check if the file already exists in the mapping table
    if (mappingManager.mappingTable.count(filename)) {
        pr_info("ERROR: Mapping already exists, refusing to overwrite file: %s", filename.c_str());
        return OPERATION_FAILURE;
    }
    if (!buffer) {
        pr_info("ERROR: Null buffer provided for write to file: %s", filename.c_str());
        return OPERATION_FAILURE;
    }

    // select_lbn() has been inserted the node ,so if operation isn't success ,the node need to remove from tree
    uint64_t lbn = lbnPoolManager.select_lbn(DISPATCH_POLICY,request);
    auto node = tree.find_node(filename,level,rangeMin,rangeMax);
    if (lbn == INVALIDLBN) {
        tree.remove_node(node);
        pr_info("Failed to allocate LBN for file: %s", filename.c_str());
        return OPERATION_FAILURE;
    }
    if(!node){
        pr_info("Find node is error,filename: %s ", filename.c_str());
        return OPERATION_FAILURE;
    }
    pr_info("Allocated LBN %lu for file: %s", lbn, filename.c_str());
    err = persistenceManager.flushSStable(lbn, (uint8_t *)buffer, BLOCK_SIZE);

    // The SStable flush to SSD is success,record the SStable store in which channel and update mappingtable
    if(err == OPERATION_SUCCESS){
        pr_info("Write block to LBN %lu in CH[%d] for file: %s successfully", lbn, LBN2CH(lbn),filename.c_str());
        node->channelInfo = LBN2CH(lbn);
        mappingManager.insert_mapping(filename, lbn);
    } 
    else {
        tree.remove_node(node);
        pr_info("Failed to write block to LBN %lu for file: %s", lbn, filename.c_str());
        return OPERATION_FAILURE;
    }
    request.lbn = lbn;
    return OPERATION_SUCCESS;
}

int IMS_interface::read_sstable(hostInfo request, uint8_t *buffer) {
    std::string filename = request.filename;
    if (mappingManager.mappingTable.count(filename) == 0) {
        pr_info("ERROR: File %s not found in mapping table", filename.c_str());
        return OPERATION_FAILURE;
    }
    if (!buffer) {
        pr_info("ERROR: Null buffer provided to read file: %s", filename.c_str());
        return OPERATION_FAILURE;
    }
    uint64_t lbn = mappingManager.mappingTable[filename];
    int err =  persistenceManager.readSStable(lbn, (uint8_t *)buffer, BLOCK_SIZE);

    if (err == OPERATION_SUCCESS) {
        pr_info("Read data from LBN %lu for file: %s successfully", lbn, filename.c_str());
    } else {
        pr_info("Failed to read block from LBN %lu for file: %s", lbn, filename.c_str());
        return OPERATION_FAILURE;
    }
    return OPERATION_SUCCESS;
}

int IMS_interface::search_key(int key) {
    if(key < 0){
        return OPERATION_FAILURE;
    }
    // The queue is sorted from low to high level.
    std::queue<std::shared_ptr<TreeNode>> list = tree.search_key(key);
}


// TODO now is not complete still need to finish  
// This function is used to allocate a block for value log
int IMS_interface::allocate_block(hostInfo request){
    uint64_t lbn;
    lbn = lbnPoolManager.allocate_valueLog_block();
    if(lbn == INVALIDLBN){
        pr_info("Allocate value log block is failed, need to check policy or free block list");
        return OPERATION_FAILURE;
    }

    return OPERATION_SUCCESS;
}

int IMS_interface::init_IMS(){
    int err = OPERATION_FAILURE;
    pr_info("Initialize IMS interface");
    persistenceManager.disk.open("disk.bin");
    uint8_t *buffer = (uint8_t *)malloc(IMS_PAGE_SIZE);
    if(!buffer){
        pr_info("Buffer malloc failed");
        return OPERATION_FAILURE;
    }
    err = persistenceManager.disk.read(0, buffer);
    if(err == OPERATION_FAILURE){
        free(buffer);
        pr_info("Read super page failed");
        return OPERATION_FAILURE;
    }
    super_page *sp = (super_page *)buffer;
    if(sp == nullptr){
        free(buffer);
        pr_info("Super page pointer is nullptr");
        return OPERATION_FAILURE;
    }
    if(sp->magic != MAGIC){
        pr_info("Magic number mismatch,this disk maybe is new or not IMS disk");
        pr_info("try to initialize IMS interface with new super page");
        sp_ptr_old->magic = MAGIC;
        sp_ptr_old->mapping_page_num = 0;
        sp_ptr_old->log_page_num = 0;
        sp_ptr_old->currentLogLBN = INVALIDLBN;
        sp_ptr_old->nextLogLBN = INVALIDLBN;
        sp_ptr_old->logOffset = 0;
        sp_ptr_old->usedLBN_num = 0;
        lbnPoolManager.lastUsedChannel = INVALIDCH;
    }
    else{
        pr_info("Super page magic number is correct, initializing IMS interface");
        sp_ptr_old->magic = sp->magic;
        // sp_ptr_old->mapping_store = sp->mapping_store;
        sp_ptr_old->mapping_page_num = sp->mapping_page_num;
        // sp_ptr_old->log_store = sp->log_store;
        sp_ptr_old->log_page_num = sp->log_page_num;
        sp_ptr_old->currentLogLBN = sp->currentLogLBN;
        sp_ptr_old->nextLogLBN = sp->nextLogLBN;
        sp_ptr_old->logOffset = sp->logOffset;
        sp_ptr_old->usedLBN_num = sp->usedLBN_num;
        lbnPoolManager.lastUsedChannel = sp->lastUsedChannel;
        err = mappingManager.init_mapping_table(sp_ptr_old->mapping_store,sp_ptr_old->mapping_page_num);
        if(err != OPERATION_SUCCESS){
            pr_info("Initialize mapping table failed");
            free(buffer);
            return OPERATION_FAILURE;
        }
        err = logManager.init_logRecordList(sp_ptr_old->log_store,sp_ptr_old->log_page_num);
        if(err != OPERATION_SUCCESS){
            pr_info("Initialize log record list failed");
            free(buffer);
            return OPERATION_FAILURE;
        }
    }
    err = lbnPoolManager.init_lbn_pool(sp_ptr_old->usedLBN_num);
    if(err != OPERATION_SUCCESS){
        pr_info("Initialize LBN pool failed");
        free(buffer);
        return OPERATION_FAILURE;
    }
    // Avoid using specific LBN (super block ,mapping block ,log block)
    lbnPoolManager.remove_freeLBNList(sp_ptr_old->mapping_store);
    lbnPoolManager.remove_freeLBNList(sp_ptr_old->log_store);
    lbnPoolManager.remove_freeLBNList(SUPER_BLOCK);

    logManager.currentLogLBN = sp_ptr_old->currentLogLBN;
    logManager.nextLogLBN = sp_ptr_old->nextLogLBN;
    logManager.logOffset = sp_ptr_old->logOffset;
    free(buffer);
    return OPERATION_SUCCESS;
}
int IMS_interface::close_IMS(){
    int err = OPERATION_FAILURE;
    pr_info("Close IMS interface");

    err = persistenceManager.flushMappingTable(mappingManager.mappingTable);
    if(err != OPERATION_SUCCESS){
        pr_info("Flushing mapping table to disk failed");
        return OPERATION_FAILURE;
    }

    err = logManager.flush_logRecordList();
    if(err != OPERATION_SUCCESS){
        pr_info("Flushing log record list to disk failed");
        return OPERATION_FAILURE;
    }
    uint8_t *buffer = (uint8_t *)malloc(IMS_PAGE_SIZE);
    if(!buffer){
        pr_info("Allocating memory for super page buffer failed");
        return OPERATION_FAILURE;
    }
    memset(buffer, 0, IMS_PAGE_SIZE);
    super_page *sp = (super_page *)buffer;
    sp->magic = MAGIC;
    sp->mapping_store = sp_ptr_old->mapping_store;
    sp->mapping_page_num = sp_ptr_new->mapping_page_num;
    sp->log_store = sp_ptr_old->log_store;
    sp->log_page_num = sp_ptr_new->log_page_num;
    sp->currentLogLBN = logManager.currentLogLBN;
    sp->nextLogLBN = logManager.nextLogLBN;
    sp->logOffset = logManager.logOffset;

    size_t total_used_lbn = 0;
    for (const auto& q : lbnPoolManager.usedLBNList) {
        total_used_lbn += q.size();
    }
    pr_info("Used lbn num:%d",total_used_lbn);
    
    for(const auto& q : lbnPoolManager.usedLBNList){
        for(auto lbn:q){
            pr_info("Used list LBN:%lld in CH[%d]",lbn,LBN2CH(lbn));
        }
    }
    sp->usedLBN_num = total_used_lbn;

    err = persistenceManager.disk.write(0, buffer);
    if(err != OPERATION_SUCCESS){
        pr_info("Writing super page to disk failed");
        free(buffer);
        return OPERATION_FAILURE;
    }
    
    free(buffer);

    persistenceManager.disk.close();
    tree.clear();
    lbnPoolManager.clear();
    mappingManager.clear();
    logManager.clear();
    delete sp_ptr_old;
    delete sp_ptr_new;
    return OPERATION_SUCCESS;
}
// TODO now not complete still need to modify
// int IMS_interface::write_log(valueLogInfo request,uint8_t *buffer){
//     int err = OPERATION_FAILURE;
//     uint64_t lpn = LBN2LPN(request.lbn) + request.page_offset;
//     err = disk.write(lpn,buffer);
//     if( err != OPERATION_SUCCESS){
//         pr_info("Write value log is fail");
//     }
//     return err;
// }

// int IMS_interface::read_log(valueLogInfo request,uint8_t *buffer){
//     int err = OPERATION_FAILURE;
//     uint64_t lpn = LBN2LPN(request.lbn) + request.page_offset;
//     err = disk.read(lpn,buffer);
//     if( err != OPERATION_SUCCESS){
//         pr_info("Read value log is fail");
//     }
//     return err;
// }