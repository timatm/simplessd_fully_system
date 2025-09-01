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


IMS_interface::IMS_interface() {
    pr_info("Constructing IMS_interface...");

    sp_ptr_old_ = new super_page(0, 1, 2);  
    sp_ptr_new_ = new super_page(0, 1, 2);
    lbnPool_ = std::make_unique<LBNPool>();
    tree_ = std::make_shared<Tree>();
    #if RUNTYPE_SIMPLESSD
    #else
        disk_.open("test.img");
    #endif

    persistenceManager_ = std::make_unique<Persistence>(&disk_, sp_ptr_old_, sp_ptr_new_, *tree_);


    mappingTable_ = std::make_unique<Mapping>(*persistenceManager_, *lbnPool_, *tree_);

    logManager_ = std::make_unique<Log>(*persistenceManager_, *lbnPool_, sp_ptr_old_, sp_ptr_new_);

    lsmTree_ = std::make_unique<LSMTree>(tree_);

    init_IMS();
}


int IMS_interface::write_sstable(hostInfo *request, uint8_t *buffer) {
    int err = OPERATION_SUCCESS;
    std::string filename = request->filename;
    int level = request->levelInfo;
    Key rangeMin = request->rangeMin;
    Key rangeMax = request->rangeMax;

    std::cout << "Write request for Filename: " << filename.c_str() << " | Level: " << level << 
        " | Range:" << rangeMin.toString() << " ~ " << rangeMax.toString() << std::endl;

    auto mappingTable = mappingTable_->get_table();
    if (mappingTable.count(filename)) {
        pr_debug("ERROR: Mapping already exists, refusing to overwrite file: %s", filename.c_str());
        return OPERATION_FAILURE;
    }

    if (!buffer) {
        pr_debug("ERROR: Null buffer provided for write to file: %s", filename.c_str());
        return OPERATION_FAILURE;
    }
    // 建立 TreeNode 並插入
    auto newNode = std::make_shared<TreeNode>(filename, level, rangeMin, rangeMax);
    lsmTree_->insert_sstable(newNode);

    // 找出該節點
    auto node = lsmTree_->find_node(filename, level, rangeMin, rangeMax);
    if (!node) {
        pr_debug("Find node is error, filename: %s ", filename.c_str());
        return OPERATION_FAILURE;
    }

    // 取得相關 channel 清單
    std::vector<int> relateList = lsmTree_->get_relate_ch_info(node);
    // 呼叫 my_policy()
    uint64_t lbn = INVALIDLBN;
    if(level > 0 && level <= MAX_LEVEL){
        switch(SELECT_POLICT){
            case static_cast<int>(SelectT::WROSTCASE):
                lbn = lbnPool_->worst_policy();
                break;
            case static_cast<int>(SelectT::RR):
                lbn = lbnPool_->RRpolicy();
                break;
            case static_cast<int>(SelectT::LEVEL2CH):
                lbn = lbnPool_->level2CH(level);
                break;
            case static_cast<int>(SelectT::MYPOLICY):
                lbn = lbnPool_->my_policy(relateList);
                break;
            default:
                pr_info("The type of policy is invalid ,check your pass parameter");
                return INVALIDLBN;
        }
    }
    else if (level == 0){
        lbn = lbnPool_->RRpolicy();
    }
    else{
        pr_debug("Level info is not correct (%d)", level);
        return OPERATION_FAILURE;
    }

    

    if (lbn == INVALIDLBN) {
        lsmTree_->remove_sstable(node);
        pr_debug("Failed to allocate LBN for file: %s", filename.c_str());
        return OPERATION_FAILURE;
    }

    if (!node) {
        pr_debug("Find node is error, filename: %s ", filename.c_str());
        return OPERATION_FAILURE;
    }

    pr_info("Allocated LBN %lu for file: %s", lbn, filename.c_str());

    if (ENABLE_DISK) {
        err = persistenceManager_->flushSStable(lbn, buffer, BLOCK_SIZE);
    }

    if (err == OPERATION_SUCCESS) {
        pr_info("Write block to LBN %lu in CH[%d] for file: %s successfully", 
                lbn, LBN2CH(lbn), filename.c_str());

        node->channelInfo = LBN2CH(lbn);
        mappingTable_->insert_mapping(filename, lbn);
    } else {
        lsmTree_->remove_sstable(node);
        pr_debug("Failed to write block to LBN %lu for file: %s", lbn, filename.c_str());
        return OPERATION_FAILURE;
    }
    pr_info("Write success return");
    return OPERATION_SUCCESS;
}

int IMS_interface::read_sstable(hostInfo *request, uint8_t *buffer) {
    int err = OPERATION_SUCCESS;
    std::string filename = request->filename;

    // 檢查 mapping table 是否有紀錄
    auto mappingTable = mappingTable_->get_table();
    if (mappingTable.count(filename) == 0) {
        pr_debug("ERROR: File %s not found in mapping table", filename.c_str());
        return OPERATION_FAILURE;
    }

    if (!buffer) {
        pr_debug("ERROR: Null buffer provided to read file: %s", filename.c_str());
        return OPERATION_FAILURE;
    }

    auto it = mappingTable.find(filename);
    if (it == mappingTable.end()) {
        pr_debug("ERROR: File %s not found in mapping table", filename.c_str());
        request->lbn = INVALIDLBN;
        return OPERATION_FAILURE;
    }

    request->lbn = it->second;

    if (ENABLE_DISK) {
        err = persistenceManager_->readSStable(request->lbn, buffer, BLOCK_SIZE);
    }

    if (err == OPERATION_SUCCESS) {
        pr_info("Read data from LBN %lu for file: %s successfully", request->lbn, filename.c_str());
    } else {
        pr_debug("Failed to read block from LBN %lu for file: %s", request->lbn, filename.c_str());
        return OPERATION_FAILURE;
    }

    return err;
}

int IMS_interface::erase_sstable(hostInfo *request){
    int err = OPERATION_SUCCESS;
    std::string filename = request->filename;
    auto lbn = mappingTable_->getLBN(filename);
    err = persistenceManager_->eraseSStable(lbn);
    if(err == OPERATION_SUCCESS){
        mappingTable_->remove_mapping(filename);
    }
    return err;
}

int IMS_interface::read_ssKeyRange(hostInfo *request, uint8_t* buffer){
    int err = OPERATION_SUCCESS;
    std::string filename = request->filename;

    auto mappingTable = mappingTable_->get_table();
    if (mappingTable.count(filename) == 0) {
        pr_debug("ERROR: File %s not found in mapping table", filename.c_str());
        return OPERATION_FAILURE;
    }

    if (!buffer) {
        pr_debug("ERROR: Null buffer provided to read file: %s", filename.c_str());
        return OPERATION_FAILURE;
    }

    auto it = mappingTable.find(filename);
    if (it == mappingTable.end()) {
        pr_debug("ERROR: File %s not found in mapping table", filename.c_str());
        request->lbn = INVALIDLBN;
        return OPERATION_FAILURE;
    }

    request->lbn = it->second;

    if (ENABLE_DISK) {
        err = persistenceManager_->readLog(LBN2LPN(request->lbn), buffer, IMS_PAGE_SIZE);
    }

    if (err == OPERATION_SUCCESS) {
        pr_info("Read data from LBN %lu for file: %s successfully", request->lbn, filename.c_str());
    } else {
        pr_debug("Failed to read block from LBN %lu for file: %s", request->lbn, filename.c_str());
        return OPERATION_FAILURE;
    }

    return err;
}

int IMS_interface::search_key(Key key) {
    if (key.key_size < 0) {
        pr_debug("ERROR: Invalid key size");
        return OPERATION_FAILURE;
    }

    // 從 LSMTree 搜尋 key，queue 是依 level 由低到高排序
    std::queue<std::shared_ptr<TreeNode>> candidates = lsmTree_->search_key(key);

    if (candidates.empty()) {
        pr_debug("Key not found in any SSTable");
        return OPERATION_FAILURE;
    }

    // 可選擇：處理 top candidate 或遍歷所有節點進一步搜尋
    // 這裡我們假設只印出 channel info 作為示範
    while (!candidates.empty()) {
        auto node = candidates.front(); candidates.pop();
        pr_info("Candidate file: %s in Level %d, Channel %d",
                node->filename.c_str(), node->levelInfo, node->channelInfo);
    }

    return OPERATION_SUCCESS;
}

// TODO now is not complete still need to finish  
// This function is used to allocate a block for value log

int IMS_interface::allocate_block(uint64_t *l) {
    if (l == nullptr) {
        pr_debug("Output pointer is null");
        return OPERATION_FAILURE;
    }

    uint64_t lbn = lbnPool_->RRpolicy();
    if (lbn == INVALIDLBN) {
        pr_debug("Allocate value log block failed: no free block or policy issue");
        return OPERATION_FAILURE;
    }
    logManager_->insert_logRecord(lbn);
    logManager_->currentLogLBN = logManager_->nextLogLBN;
    logManager_->nextLogLBN = lbn;
    logManager_->logOffset = 0;
    *l = lbn;
    pr_info("Allocated LBN: %lu", lbn);
    return OPERATION_SUCCESS;
}



int IMS_interface::rebuild_super_page() {
    pr_info("Try to initialize IMS interface with new super page");

    sp_ptr_old_->magic = MAGIC;
    sp_ptr_old_->mapping_page_num = 0;
    sp_ptr_old_->log_page_num = 0;
    sp_ptr_old_->currentLogLBN = lbnPool_->RRpolicy();
    sp_ptr_old_->nextLogLBN = lbnPool_->RRpolicy();
    get_logManager()->insert_logRecord(sp_ptr_old_->currentLogLBN);
    get_logManager()->insert_logRecord(sp_ptr_old_->nextLogLBN);
    sp_ptr_old_->logOffset = 0;
    sp_ptr_old_->usedLBN_num = 0;
    sp_ptr_old_->global_sequence = 0;
    sp_ptr_old_->sstable_sequence = 0;
    sp_ptr_old_->lastUsedChannel = 0;
    uint8_t lastUsedChannel;
    lbnPool_->set_lastUsedChannel(0);
    return OPERATION_SUCCESS;
}

int IMS_interface::init_IMS() {
    int err = OPERATION_FAILURE;
    pr_info("Initialize IMS interface");

    uint8_t* buffer = (uint8_t*)malloc(IMS_PAGE_SIZE);
    if (!buffer) {
        pr_debug("Buffer malloc failed");
        return OPERATION_FAILURE;
    }

    err = disk_.readPage(0, buffer);
    if (err == OPERATION_FAILURE) {
        free(buffer);
        pr_debug("Read super page failed");
        return OPERATION_FAILURE;
    }

    super_page* sp = (super_page*)buffer;
    if (sp == nullptr) {
        free(buffer);
        pr_debug("Super page pointer is nullptr");
        return OPERATION_FAILURE;
    }
    std::vector<uint64_t> used_lbns;
    int usedLBN = 0;
    if (sp->magic != MAGIC) {
        pr_info("Magic number mismatch, this disk maybe is new or not IMS disk");
        usedLBN = lbnPool_->init_lbn_pool(used_lbns);
        lbnPool_->remove_freeLBNList(sp_ptr_old_->mapping_store);
        lbnPool_->remove_freeLBNList(sp_ptr_old_->log_store);
        lbnPool_->remove_freeLBNList(SUPER_BLOCK);
        rebuild_super_page();
    } 
    else {
        pr_info("Super page magic number is correct, initializing IMS interface");

        *sp_ptr_old_ = *sp;
        *sp_ptr_old_ = *sp;
        lbnPool_->set_lastUsedChannel(sp_ptr_old_->lastUsedChannel);
        sp_ptr_old_->dump();
        err = mappingTable_->init_mapping_table(sp_ptr_old_->mapping_store, sp_ptr_old_->mapping_page_num);
        if (err != OPERATION_SUCCESS) {
            pr_debug("Initialize mapping table failed");
            free(buffer);
            return OPERATION_FAILURE;
        }

        pr_info("InitLogRecordList: logStoreLBN = %lu", sp_ptr_old_->log_store);
        err = logManager_->init_logRecordList(sp_ptr_old_->log_store, sp_ptr_old_->log_page_num);
        if (err != OPERATION_SUCCESS) {
            pr_debug("Initialize log record list failed");
            free(buffer);
            return OPERATION_FAILURE;
        }

        auto mappingTable = mappingTable_->get_table();
        for (const auto& [filename, lbn] : mappingTable) {
            used_lbns.push_back(lbn);
        }

        for (uint64_t lbn : logManager_->logRecordList) {
            used_lbns.push_back(lbn);
        }
        usedLBN = lbnPool_->init_lbn_pool(used_lbns);
        lbnPool_->remove_freeLBNList(sp_ptr_old_->mapping_store);
        lbnPool_->remove_freeLBNList(sp_ptr_old_->log_store);
        lbnPool_->remove_freeLBNList(SUPER_BLOCK);
    }
    
    if ( (usedLBN) != sp_ptr_old_->usedLBN_num) {
        pr_debug("Initialize LBN pool failed");
        free(buffer);
        return OPERATION_FAILURE;
    }
    logManager_->currentLogLBN = sp_ptr_old_->currentLogLBN;
    logManager_->nextLogLBN = sp_ptr_old_->nextLogLBN;
    logManager_->logOffset = sp_ptr_old_->logOffset;

    free(buffer);
    pr_info("Initialize LBN pool success");
    pr_info("Init_IMS is done");    
    dump_all();
    return OPERATION_SUCCESS;
}

int IMS_interface::close_IMS() {
    int err = OPERATION_FAILURE;
    pr_info("Close IMS interface");
    auto mappingTable = mappingTable_->get_table();
    err = persistenceManager_->flushMappingTable(mappingTable);
    if (err != OPERATION_SUCCESS) {
        pr_debug("Flushing mapping table to disk failed");
        return OPERATION_FAILURE;
    }

    err = logManager_->flush_logRecordList();
    if (err != OPERATION_SUCCESS) {
        pr_debug("Flushing log record list to disk failed");
        return OPERATION_FAILURE;
    }

    uint8_t* buffer = (uint8_t*)malloc(IMS_PAGE_SIZE);
    if (!buffer) {
        pr_debug("Allocating memory for super page buffer failed");
        return OPERATION_FAILURE;
    }

    memset(buffer, 0, IMS_PAGE_SIZE);
    super_page* sp = (super_page*)buffer;

    sp->magic = MAGIC;
    sp->mapping_store = sp_ptr_old_->mapping_store;
    sp->mapping_page_num = sp_ptr_new_->mapping_page_num;
    sp->log_store = sp_ptr_old_->log_store;
    sp->log_page_num = sp_ptr_new_->log_page_num;
    sp->currentLogLBN = logManager_->currentLogLBN;
    sp->nextLogLBN = logManager_->nextLogLBN;
    sp->logOffset = logManager_->logOffset;

    size_t total_used_lbn = 0;
    for (const auto& q : lbnPool_->get_usedLBNList()) {
        total_used_lbn += q.size();
    }

    // pr_info("Used lbn num: %zu", total_used_lbn);
    // for (const auto& q : lbnPool_->get_usedLBNList()) {
    //     for (auto lbn : q) {
    //         pr_info("Used list LBN: %lld in CH[%d]", lbn, LBN2CH(lbn));
    //     }
    // }
    sp->usedLBN_num = total_used_lbn;

    err = disk_.writePage(0, buffer);
    if (err != OPERATION_SUCCESS) {
        pr_info("Writing super page to disk failed");
        free(buffer);
        return OPERATION_FAILURE;
    }

    free(buffer);
    // lsmTree_->clear();             
    lbnPool_->clear();             
    mappingTable_->clear();      
    logManager_->clear();        
    reset_superPage(sp_ptr_old_);
    reset_superPage(sp_ptr_new_);

    return OPERATION_SUCCESS;
}
int IMS_interface::init_DB(uint8_t *buffer){
    uint32_t next_lbn = get_logManager()->nextLogLBN;
    uint32_t current_lbn = get_logManager()->currentLogLBN;
    uint32_t page_offset = get_logManager()->logOffset;
    
}
// TODO now not complete still need to modify
int IMS_interface::write_log(uint64_t lpn,uint8_t *buffer){
    // pr_info("Write log at LPN: %lu in IMS", lpn);
    if (buffer == nullptr) {
        pr_debug("Read log failed: null request or buffer");
        return OPERATION_FAILURE;
    }
    int err = OPERATION_SUCCESS;
    err = get_persistenceManager()->writeLog(lpn,buffer,IMS_PAGE_SIZE);
    if( err != OPERATION_SUCCESS){
        pr_debug("Write value log failed at LPN %lu", lpn);
    }
    return err;
}

int IMS_interface::read_log(uint64_t lpn,uint8_t *buffer){
    if (buffer == nullptr) {
        pr_debug("Read log failed: null request or buffer");
        return OPERATION_FAILURE;
    }
    int err = OPERATION_SUCCESS;
    err = get_persistenceManager()->readLog(lpn,buffer,IMS_PAGE_SIZE);
    if( err != OPERATION_SUCCESS){
        pr_debug("Read value log failed at LPN %lu", lpn);
    }
    return err;
}

void IMS_interface::reset_superPage(super_page *sp) {
    if (sp == nullptr) {
        pr_debug("Super page pointer is null, cannot reset");
        return;
    }
    sp->magic = 0;
    sp->mapping_store = 0; // Default mapping store LBN
    sp->mapping_page_num = 0;
    sp->log_store = 0; // Default log store LBN
    sp->log_page_num = 0;
    sp->currentLogLBN = INVALIDLBN;
    sp->nextLogLBN = INVALIDLBN;
    sp->logOffset = INVALIDLBN;
    sp->usedLBN_num = 0;
    sp->lastUsedChannel = INVALIDCH;
}

int IMS_interface::reset_IMS(){
    super_page sp(0,0,0);
    uint8_t *buffer = reinterpret_cast<uint8_t*>(&sp);
    int err = disk_.writePage(0, buffer);
    if (err != OPERATION_SUCCESS) {
        pr_debug("Writing super page to disk failed");
        free(buffer);
        return OPERATION_FAILURE;
    }
    return OPERATION_SUCCESS;
}

int IMS_interface::dump_IMS(){
    dump_all();
    return OPERATION_SUCCESS;
}

int IMS_interface::open_DB(uint8_t* buffer, size_t buffer_size) {
    std::string result;
    DB_INIT info;

    get_oldSuperPage()->dump();

    info.current_lbn  = get_logManager()->currentLogLBN;
    info.next_lbn     = get_logManager()->nextLogLBN;
    info.page_offset  = get_logManager()->logOffset;
    info.global_seq   = get_oldSuperPage()->global_sequence;
    info.sstable_seq  = get_oldSuperPage()->sstable_sequence;
    info.log_list     = get_logManager()->encode();
    info.node_list    = get_lsmTree()->encode();
    result = info.encode();

    if (result.size() > buffer_size) return OPERATION_FAILURE;  
    std::memcpy(buffer, result.data(), result.size());

    return OPERATION_SUCCESS;
}


int write_metadata(uint8_t *buffer, size_t size){
    if (buffer == nullptr || size == 0) {
        pr_debug("Write metadata failed: null buffer or size is zero");
        return OPERATION_FAILURE;
    }

    int err = OPERATION_SUCCESS;
    if (err != OPERATION_SUCCESS) {
        return OPERATION_FAILURE;
    }
    return OPERATION_SUCCESS;
}