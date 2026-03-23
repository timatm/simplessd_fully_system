#include "IMS_interface.hh"
#include <algorithm>
#include <numeric>
#include <mutex>
#include "def.hh"
#include "lbn_pool.hh"
#include "persistence.hh"
#include "tree.hh"
#include "mapping_table.hh"
#include "print.hh"
#include "log.hh"

#if RUNTYPE
void IMS_interface::attachDisk(SimpleSSD::Disk* d) {
    disk_ = d;

    if (persistenceManager_) {
        // 直接更新底層的 pDisk_
        persistenceManager_->pDisk_ = d;
    } else {
        // 若你打算在 RUNTYPE=1 不在建構子裡建 Persistence，也可以在這邊建
        persistenceManager_ = std::make_unique<Persistence>(disk_, sp_ptr_, *tree_);
        mappingTable_ = std::make_unique<Mapping>(*persistenceManager_, *lbnPool_, *tree_);
        logManager_   = std::make_unique<Log>(*persistenceManager_, *lbnPool_, sp_ptr_);
    }
}
#endif
void PrintBuildConfig() {
    // ---- RUNTYPE 說明 ----
    const char* runtype_str =
    #if RUNTYPE == 0
        "Host environment (RUNTYPE=0)";
    #elif RUNTYPE == 1
        "SimpleSSD environment (RUNTYPE=1)";
    #else
        "UNKNOWN RUNTYPE (should be 0 or 1)";
    #endif
    ;

    const char* enable_disk_str = ENABLE_DISK ? "enabled" : "disabled";

    const char* nvme_driver_str =
    #if RUNTYPE == 0
        "Host/test NVMe driver";
    #elif RUNTYPE == 1
        "SimpleSSD NVMe backend";
    #else
        "UNKNOWN NVME driver mode";
    #endif
    ;

    // ---- SELECT_POLICY 說明 ----
    const char* select_policy_str = nullptr;
    switch (SELECT_POLICY) {
        case 0: select_policy_str = "WROSTCASE"; break;
        case 1: select_policy_str = "RR";        break;
        case 2: select_policy_str = "LEVEL2CH";  break;
        case 3: select_policy_str = "MYPOLICY";  break;
        default: select_policy_str = "UNKNOWN";  break;
    }

    // ---- PACKING_TYPE 說明 ----
    const char* packing_type_str = nullptr;

#if RUNTYPE == 0
    #ifdef PACKING_TYPE
        switch (PACKING_TYPE) {
            case 0: packing_type_str = "kKeyPerPage"; break;
            case 1: packing_type_str = "kHash";       break;
            case 2: packing_type_str = "kKeyRange";   break;
            case 3: packing_type_str = "kIndexFilter";   break;
            default: packing_type_str = "UNKNOWN";    break;
        }
    #endif
        pr_info("========== SSD Config ==========");
        pr_info("RUNTYPE        = %d (%s)", RUNTYPE, runtype_str);
        pr_info("ENABLE_DISK    = %d (%s)", ENABLE_DISK, enable_disk_str);
        pr_info("NVME_DRIVER    = %d (%s)", NVME_DRIVER, nvme_driver_str);
        pr_info("SELECT_POLICY  = %d (%s)", SELECT_POLICY, select_policy_str);
    #ifdef PACKING_TYPE
        pr_info("PACKING_TYPE   = %d (%s)", PACKING_TYPE, packing_type_str);
    #endif
    // ---- SSD / IMS 幾何資訊 ----
        // ---- SSD / IMS 幾何資訊 ----
    pr_info("---------- IMS / SSD Geometry ----------");
    pr_info("CHANNEL_NUM    = %d (bits=%d)", CHANNEL_NUM, CHANNEL_BITS);
    pr_info("PACKAGE_NUM    = %d (bits=%d)", PACKAGE_NUM, PACKAGE_BITS);
    pr_info("DIE_NUM        = %d (bits=%d)", DIE_NUM,     DIE_BITS);
    pr_info("PLANE_NUM      = %d (bits=%d)", PLANE_NUM,   PLANE_BITS);
    pr_info("BLOCK_NUM      = %d (bits=%d)", BLOCK_NUM,   BLOCK_BITS);

    // 換成 K / M 單位
    const int page_kb  = IMS_PAGE_SIZE / 1024;
    const int block_mb = BLOCK_SIZE    / (1024 * 1024);
    

    pr_info("IMS_PAGE_NUM   = %d", IMS_PAGE_NUM);
    pr_info("IMS_PAGE_SIZE  = %d KB", page_kb);
    pr_info("BLOCK_SIZE     = %d MB", block_mb);

    // int lbn_num = (int)((double)LBN_NUM* (1-SSD_PROVISION_RATIO));
    pr_info("SSD SUPER PROVISION RATIO = %f",SSD_PROVISION_RATIO);
    pr_info("LBN_NUM        = %d", LBN_NUM);
    pr_info("LBN_SIZE       = %d MB", LBN_SIZE / (1024 * 1024));
    pr_info("LPN_NUM        = %d", LPN_NUM);

    // SSD 總容量，用 G 表示
    unsigned long long total_bytes =
        (unsigned long long)LBN_NUM * (unsigned long long)BLOCK_SIZE;
    double total_gib = (double)total_bytes / (1024.0 * 1024.0 * 1024.0);

    pr_info("SSD capacity   = %.2f GiB (%llu bytes)",
            total_gib,
            (unsigned long long)total_bytes);

    pr_info("========================================");
#else
    #ifdef PACKING_TYPE
        switch (PACKING_TYPE) {
            case 0: packing_type_str = "kKeyPerPage"; break;
            case 1: packing_type_str = "kHash";       break;
            case 2: packing_type_str = "kKeyRange";   break;
            case 3: packing_type_str = "kIndexFilter";   break;
            default: packing_type_str = "UNKNOWN";    break;
        }
    #endif

    MYDB_LOG("========== SSD Config ==========");
    MYDB_LOG("RUNTYPE        = %d (%s)", RUNTYPE, runtype_str);
    MYDB_LOG("ENABLE_DISK    = %d (%s)", ENABLE_DISK, enable_disk_str);
    MYDB_LOG("NVME_DRIVER    = %d (%s)", NVME_DRIVER, nvme_driver_str);
    MYDB_LOG("SELECT_POLICY  = %d (%s)", SELECT_POLICY, select_policy_str);

    #ifdef PACKING_TYPE
        MYDB_LOG("PACKING_TYPE   = %d (%s)", PACKING_TYPE, packing_type_str);
    #endif
    
    MYDB_LOG("---------- IMS / SSD Geometry ----------");
    MYDB_LOG("CHANNEL_NUM    = %d (bits=%d)", CHANNEL_NUM, CHANNEL_BITS);
    MYDB_LOG("PACKAGE_NUM    = %d (bits=%d)", PACKAGE_NUM, PACKAGE_BITS);
    MYDB_LOG("DIE_NUM        = %d (bits=%d)", DIE_NUM,     DIE_BITS);
    MYDB_LOG("PLANE_NUM      = %d (bits=%d)", PLANE_NUM,   PLANE_BITS);
    MYDB_LOG("BLOCK_NUM      = %d (bits=%d)", BLOCK_NUM,   BLOCK_BITS);

    // 換成 K / M 單位
    const int page_kb  = IMS_PAGE_SIZE / 1024;
    const int block_mb = BLOCK_SIZE    / (1024 * 1024);
    

    MYDB_LOG("IMS_PAGE_NUM   = %d", IMS_PAGE_NUM);
    MYDB_LOG("IMS_PAGE_SIZE  = %d KB", page_kb);
    MYDB_LOG("BLOCK_SIZE     = %d MB", block_mb);

    // int lbn_num = (int)((double)LBN_NUM* (1-SSD_PROVISION_RATIO));
    MYDB_LOG("SSD SUPER PROVISION RATIO = %f",SSD_PROVISION_RATIO);
    MYDB_LOG("LBN_NUM        = %d", LBN_NUM);
    MYDB_LOG("LBN_SIZE       = %d MB", LBN_SIZE / (1024 * 1024));
    MYDB_LOG("LPN_NUM        = %d", LPN_NUM);

    // SSD 總容量，用 G 表示
    unsigned long long total_bytes =
        (unsigned long long)LBN_NUM * (unsigned long long)BLOCK_SIZE;
    double total_gib = (double)total_bytes / (1024.0 * 1024.0 * 1024.0);

    MYDB_LOG("SSD capacity   = %.2f GiB (%llu bytes)",
            total_gib,
            (unsigned long long)total_bytes);

    MYDB_LOG("========================================");
#endif

}


void IMS_interface::alloc_device_buffer(size_t bytes) {
    if (buffer_) return;
    pr_debug("alloc_device_buffer: buffer_=%p, bytes=%zu", (void*)buffer_, bytes);
    buffer_ = static_cast<uint8_t*>(aligned_alloc_4k(bytes));
    if (!buffer_) throw std::bad_alloc();
    buffer_size_ = bytes;
    std::memset(buffer_, 0, buffer_size_);
    pr_debug("alloc_device_buffer: buffer_=%p, bytes=%zu", (void*)buffer_, bytes);
}

void IMS_interface::free_device_buffer() {
    if (buffer_) {
        aligned_free_4k(buffer_);
        buffer_ = nullptr;
    }
    buffer_size_ = 0;
}

int IMS_interface::reset_IMS_buffer() {
    std::lock_guard<std::mutex> lk(buf_mu_);
    if (buffer_ && buffer_size_) std::memset(buffer_, 0, buffer_size_);
    // 其他状态重置...
    return 0;
}

IMS_interface::IMS_interface() {
    PrintBuildConfig();
    pr_debug("Constructing IMS_interface...");
    sp_ptr_ = new super_page(0, 1, 2);  
    lbnPool_ = std::make_unique<LBNPool>();
    tree_ = std::make_shared<Tree>();
    #if RUNTYPE
    #else
        disk_.open("test.img");
    #endif

    // persistenceManager_ = std::make_unique<Persistence>(&disk_, sp_ptr_old_, sp_ptr_new_, *tree_);
    #if RUNTYPE
        persistenceManager_ = std::make_unique<Persistence>(disk_, sp_ptr_, *tree_);
    #else
        persistenceManager_ = std::make_unique<Persistence>(&disk_, sp_ptr_ , *tree_);
    #endif

    mappingTable_ = std::make_unique<Mapping>(*persistenceManager_, *lbnPool_, *tree_);

    logManager_ = std::make_unique<Log>(*persistenceManager_, *lbnPool_, sp_ptr_);

    lsmTree_ = std::make_unique<LSMTree>(tree_);
    buffer_size_ = 0;
    buffer_valid_size_ = 0;
    alloc_device_buffer(kDefaultDeviceDramSize);
    // init_IMS();
    #if RUNTYPE == 0
        // 只有 host 測試時才在建構子就 init
        init_IMS();
    #endif
    sstable_count_per_ch.clear();
    for(int i = 0;i < CHANNEL_NUM;i++){
        sstable_count_per_ch.push_back(0);
    }
}

IMS_interface::~IMS_interface() {
    pr_info("IMS close in destructor");
    try {
        int err = close_IMS();
        if (err != OPERATION_SUCCESS) {
            pr_error("IMS close failed in destructor");
        } else {
            pr_info("IMS close is successful in destructor");
        }
    } catch (const std::exception &e) {
        pr_error("IMS destructor: close_IMS threw exception: %s", e.what());
    }
    dump_lsm_tree();
    print_result();
    delete sp_ptr_;
}



int IMS_interface::write_sstable(uint64_t &lbn) {
    int err = OPERATION_SUCCESS;
    if (buffer_ == nullptr || buffer_valid_size_ == 0) {
        pr_error("write_sstable: buffer_ is null or buffer_valid_size_ == 0");
        return OPERATION_FAILURE;
    }
    size_t hostInfo_len = buffer_valid_size_;
    std::string buf(buffer_, buffer_ + hostInfo_len);
    hostInfo request = hostInfo::decodeOrThrow(buf);
    // pr_debug("[FW] Write request for Filename: %s | Level: %d",
    //          request.filename.c_str(), request.levelInfo);

    // pr_debug("[FW] rangeMin (string) = '%s'", request.rangeMin.toString().c_str());
    // pr_debug("[FW] rangeMax (string) = '%s'", request.rangeMax.toString().c_str());

    // std::cout << "[FW] rangeMin (hex): ";
    // request.rangeMin.dumpUint();
    // std::cout << "[FW] rangeMax (hex): ";
    // request.rangeMax.dumpUint();

    std::string filename = request.filename;
    int level = request.levelInfo;
    Key rangeMin = request.rangeMin;
    Key rangeMax = request.rangeMax;

    pr_debug("Write request for Filename: %s | Level: %d | Range:%s ~ %s"
            ,filename.c_str(),level,rangeMin.toString().c_str(),rangeMax.toString().c_str());

    auto mappingTable = mappingTable_->get_table();
    if (mappingTable.count(filename)) {
        pr_error("Mapping already exists, refusing to overwrite file: %s", filename.c_str());
        return OPERATION_FAILURE;
    }

    // if (!buffer) {
    //     pr_error("Null buffer provided for write to file: %s", filename.c_str());
    //     return OPERATION_FAILURE;
    // }
    // 建立 TreeNode 並插入
    auto newNode = std::make_shared<TreeNode>(filename, level, rangeMin, rangeMax);
    lsmTree_->insert_sstable(newNode);

    // 找出該節點
    auto node = lsmTree_->find_node(filename, level, rangeMin, rangeMax);
    if (!node) {
        pr_error("Find node is error, filename: %s ", filename.c_str());
        return OPERATION_FAILURE;
    }

    // 取得相關 channel 清單
    RelateChInfo relateList = lsmTree_->get_relate_ch_info(node);
    // 呼叫 my_policy()
    uint64_t selectLBN = INVALIDLBN;
    if(level > 0 && level <= MAX_LEVEL){
        switch(SELECT_POLICY){
            case static_cast<int>(SelectT::WROSTCASE):
                selectLBN = lbnPool_->worst_policy();
                break;
            case static_cast<int>(SelectT::RR):
                selectLBN = lbnPool_->RRpolicy();
                break;
            case static_cast<int>(SelectT::LEVEL2CH):
                selectLBN = lbnPool_->level2CH(level);
                break;
            case static_cast<int>(SelectT::MYPOLICY):
                selectLBN = lbnPool_->my_policy(relateList);
                break;
            default:
                pr_error("The type of policy is invalid ,check your pass parameter");
                return INVALIDLBN;
        }
    }
    else if (level == 0){
        switch(SELECT_POLICY){
            case static_cast<int>(SelectT::WROSTCASE):
            case static_cast<int>(SelectT::RR):
            case static_cast<int>(SelectT::LEVEL2CH):
                selectLBN = lbnPool_->RRpolicy();
                break;
            case static_cast<int>(SelectT::MYPOLICY):
                selectLBN = lbnPool_->my_policyL0(relateList);
                // selectLBN = lbnPool_->RRpolicy();
                break;
            default:
                pr_error("The type of policy is invalid ,check your pass parameter");
                return INVALIDLBN;
        }
    }
    else{
        pr_error("Level info is not correct (%d)", level);
        return OPERATION_FAILURE;
    }

    

    if (selectLBN == INVALIDLBN) {
        lsmTree_->remove_sstable(node);
        pr_error("Failed to allocate LBN for file: %s", filename.c_str());
        return OPERATION_FAILURE;
    }

    if (!node) {
        pr_error("Find node is error, filename: %s ", filename.c_str());
        return OPERATION_FAILURE;
    }
    lbn = selectLBN;
    pr_debug("Allocated LBN %lu (CH=%d) for file: %s",lbn, LBN2CH(lbn), filename.c_str());
    node->channelInfo = LBN2CH(lbn);
    mappingTable_->insert_mapping(filename, lbn);
    sstable_count_per_ch[node->channelInfo]++;
    total_sstable_write_count++;
    // if (ENABLE_DISK) {
    //     err = persistenceManager_->writeBlock(lbn, buffer, BLOCK_SIZE);
    // }

    // if (err == OPERATION_SUCCESS) {
    //     pr_debug("Write block to LBN %lu in CH[%d] for file: %s successfully", 
    //             lbn, LBN2CH(lbn), filename.c_str());

    //     node->channelInfo = LBN2CH(lbn);
    //     mappingTable_->insert_mapping(filename, lbn);
    // } else {
    //     lsmTree_->remove_sstable(node);
    //     pr_error("Failed to write block to LBN %lu for file: %s", lbn, filename.c_str());
    //     return OPERATION_FAILURE;
    // }
    return OPERATION_SUCCESS;
}


int IMS_interface::read_sstable(uint64_t &lbn) {
    int err = OPERATION_SUCCESS;
    if (buffer_ == nullptr || buffer_valid_size_ == 0) {
        pr_error("read_sstable: buffer_ is null or buffer_valid_size_ == 0");
        return OPERATION_FAILURE;
    }
    size_t hostInfo_len = buffer_valid_size_;
    std::string buf(buffer_, buffer_ + hostInfo_len);
    hostInfo request = hostInfo::decodeOrThrow(buf);
    std::string filename = request.filename;

    // 檢查 mapping table 是否有紀錄
    auto mappingTable = mappingTable_->get_table();
    if (mappingTable.count(filename) == 0) {
        pr_error("File %s not found in mapping table", filename.c_str());
        return OPERATION_FAILURE;
    }

    // if (!buffer) {
    //     pr_error("Null buffer provided to read file: %s", filename.c_str());
    //     return OPERATION_FAILURE;
    // }

    auto it = mappingTable.find(filename);
    if (it == mappingTable.end()) {
        pr_error("File %s not found in mapping table", filename.c_str());
        lbn = INVALID_32;
        return OPERATION_FAILURE;
    }

    lbn = it->second;

    // if (ENABLE_DISK) {
    //     err = persistenceManager_->readBlock(request.lbn, buffer, BLOCK_SIZE);
    // }

    // if (err == OPERATION_SUCCESS) {
    //     pr_debug("Read data from LBN %lu for file: %s successfully", request.lbn, filename.c_str());
    // } else {
    //     pr_error("Failed to read block from LBN %lu for file: %s", request.lbn, filename.c_str());
    //     return OPERATION_FAILURE;
    // }

    return err;
}


int IMS_interface::erase_sstable(uint64_t &lbn) {
    int err = OPERATION_SUCCESS;
    if (buffer_ == nullptr || buffer_valid_size_ == 0) {
        pr_error("erase_sstable: buffer_ is null or buffer_valid_size_ == 0");
        return OPERATION_FAILURE;
    }
    size_t hostInfo_len = buffer_valid_size_;
    std::string buf(buffer_, buffer_ + hostInfo_len);
    hostInfo request = hostInfo::decodeOrThrow(buf);
    std::string filename = request.filename;

    uint64_t mappedLBN = mappingTable_->getLBN(filename);
    if (mappedLBN == INVALIDLBN) {
        pr_error("erase_sstable: file %s not found in mapping table", filename.c_str());
        lbn = INVALIDLBN;
        return OPERATION_FAILURE;
    }

    lbn = mappedLBN;
#if RUNTYPE == 0
    err = persistenceManager_->eraseBlock(lbn);
    if (err != OPERATION_SUCCESS) {
        pr_error("erase_sstable: persistence eraseBlock failed for LBN %lu", lbn);
        return err;
    }
#endif
    if (err != OPERATION_SUCCESS) {
        pr_error("erase_sstable: eraseBlock failed, LBN=%lu, file=%s",
                 lbn, filename.c_str());
        return err;
    }
    mappingTable_->remove_mapping(filename);
    auto node = lsmTree_->find_node(filename);
    if (node) {
        lsmTree_->remove_sstable(node);
    }
    else {
        pr_error("erase_sstable: cannot find tree node for file %s", filename.c_str());
    }

    return err;
}



int IMS_interface::read_ssKeyRange(uint64_t& lpn){
    int err = OPERATION_SUCCESS;
    if (buffer_ == nullptr || buffer_valid_size_ == 0) {
        pr_error("read_ssKeyRange: buffer_ is null or buffer_valid_size_ == 0");
        return OPERATION_FAILURE;
    }
    size_t hostInfo_len = buffer_valid_size_;
    std::string buf(buffer_, buffer_ + hostInfo_len);
    hostInfo request = hostInfo::decodeOrThrow(buf);
    std::string filename = request.filename;
    auto mappingTable = mappingTable_->get_table();
    if (mappingTable.count(filename) == 0) {
        pr_error("File %s not found in mapping table", filename.c_str());
        return OPERATION_FAILURE;
    }

    

    auto it = mappingTable.find(filename);
    if (it == mappingTable.end()) {
        pr_error("File %s not found in mapping table", filename.c_str());
        return OPERATION_FAILURE;
    }

    uint64_t lbn = it->second;

    lpn = LBN2LPN(lbn);

    if (err == OPERATION_SUCCESS) {
        pr_debug("Read data from LBN %lu for file: %s successfully", lbn, filename.c_str());
    } else {
        pr_error("Failed to read block from LBN %lu for file: %s", lbn, filename.c_str());
        return OPERATION_FAILURE;
    }

    return err;
}


int IMS_interface::read_ssPage(uint64_t& lpn){
    int err = OPERATION_SUCCESS;
    if (buffer_ == nullptr || buffer_valid_size_ == 0) {
        pr_error("read_ssKeyRange: buffer_ is null or buffer_valid_size_ == 0");
        return OPERATION_FAILURE;
    }
    size_t hostInfo_len = buffer_valid_size_;
    std::string buf(buffer_, buffer_ + hostInfo_len);
    hostInfo request = hostInfo::decodeOrThrow(buf);
    std::string filename = request.filename;
    auto mappingTable = mappingTable_->get_table();
    if (mappingTable.count(filename) == 0) {
        pr_error("File %s not found in mapping table", filename.c_str());
        return OPERATION_FAILURE;
    }
    auto it = mappingTable.find(filename);
    if (it == mappingTable.end()) {
        pr_error("File %s not found in mapping table", filename.c_str());
        return OPERATION_FAILURE;
    }

    uint64_t lbn = it->second;

    lpn = LBN2LPN(lbn);

    if (err == OPERATION_SUCCESS) {
        pr_debug("Read data from LBN %lu for file: %s successfully", lbn, filename.c_str());
    } else {
        pr_error("Failed to read block from LBN %lu for file: %s", lbn, filename.c_str());
        return OPERATION_FAILURE;
    }

    return err;
}

int IMS_interface::search_key(Key key) {
    if (key.key_size < 0) {
        pr_error("Invalid key size");
        return OPERATION_FAILURE;
    }

    // 從 LSMTree 搜尋 key，queue 是依 level 由低到高排序
    std::queue<std::shared_ptr<TreeNode>> candidates = lsmTree_->search_key(key);

    if (candidates.empty()) {
        pr_error("Key not found in any SSTable");
        return OPERATION_FAILURE;
    }

    // 可選擇：處理 top candidate 或遍歷所有節點進一步搜尋
    // 這裡我們假設只印出 channel info 作為示範
    pr_info("");
    while (!candidates.empty()) {
        auto node = candidates.front(); candidates.pop();
        pr_error("Candidate file: %s in Level %d, Channel %d",
                node->filename.c_str(), node->levelInfo, node->channelInfo);
    }

    return OPERATION_SUCCESS;
}

// TODO now is not complete still need to finish  
// This function is used to allocate a block for value log

int IMS_interface::allocate_block(uint64_t *l) {
    if (l == nullptr) {
        pr_error("Output pointer is null");
        return OPERATION_FAILURE;
    }

    uint64_t lbn = lbnPool_->RRpolicyForLog();
    if (lbn == INVALIDLBN) {
        pr_error("Allocate value log block failed: no free block or policy issue");
        return OPERATION_FAILURE;
    }
    logManager_->insert_logRecord(lbn);
    logManager_->currentLogLBN = logManager_->nextLogLBN;
    logManager_->nextLogLBN = lbn;
    logManager_->logOffset = 0;
    *l = lbn;
    pr_debug("Allocated LBN: %lu", lbn);
    return OPERATION_SUCCESS;
}



int IMS_interface::rebuild_super_page() {
    pr_debug("Try to initialize IMS interface with new super page");

    sp_ptr_->magic = MAGIC;
    sp_ptr_->mapping_page_num = 0;
    sp_ptr_->log_page_num = 0;
    sp_ptr_->currentLogLBN = lbnPool_->RRpolicyForLog();
    sp_ptr_->nextLogLBN = lbnPool_->RRpolicyForLog();
    get_logManager()->insert_logRecord(sp_ptr_->currentLogLBN);
    get_logManager()->insert_logRecord(sp_ptr_->nextLogLBN);
    sp_ptr_->logOffset = 0;
    sp_ptr_->usedLBN_num = 0;
    sp_ptr_->global_sequence = 0;
    sp_ptr_->sstable_sequence = 0;
    sp_ptr_->lastUsedChannel = 0;
    uint8_t lastUsedChannel;
    lbnPool_->set_lastUsedChannel(0);
    return OPERATION_SUCCESS;
}

int IMS_interface::write_meta(uint8_t *host_buffer, size_t size){
    if (!host_buffer || size == 0) return -2;
    if (!in_range(0, size)) return -4;
    // pr_info("Write buffer (host data size:%u) (DRAM size:%u)",size,buffer_size_);
    std::lock_guard<std::mutex> lk(buf_mu_);
    std::memcpy(buffer_, host_buffer, size);
    buffer_valid_size_ = size;
    return 0;
}
int IMS_interface::read_meta(uint8_t *host_buffer, size_t size){
    
    if (!host_buffer || size == 0) return -2;
    if (size > buffer_valid_size_) return -5;
    // 可选：强制 4K 对齐
    // if ((dev_offset % 4096) || (len % 4096)) return -3;

    if (!in_range(0, size)) return -4;

    std::lock_guard<std::mutex> lk(buf_mu_);
    std::memcpy(host_buffer, buffer_, size);
    return 0;
}


int IMS_interface::init_IMS() {
    int err = OPERATION_FAILURE;
    pr_info("Initialize IMS interface");

    // init IMS buffer
    


    uint8_t* buffer = (uint8_t*)malloc(IMS_PAGE_SIZE);
    if (!buffer) {
        pr_error("Buffer malloc failed");
        return OPERATION_FAILURE;
    }
#if RUNTYPE == 1
    err = disk_->readPage(0, buffer);
#else
    err = disk_.readPage(0, buffer);
#endif

    if (err == OPERATION_FAILURE) {
        free(buffer);
        pr_error("Read super page failed");
        return OPERATION_FAILURE;
    }

    super_page* sp = (super_page*)buffer;
    if (sp == nullptr) {
        free(buffer);
        pr_error("Super page pointer is nullptr");
        return OPERATION_FAILURE;
    }
    std::vector<uint64_t> used_lbns;
    int usedLBN = 0;
    if (sp->magic != MAGIC) {
        pr_info("Magic number mismatch, this disk maybe is new or not IMS disk");
        usedLBN = lbnPool_->init_lbn_pool(used_lbns);
        lbnPool_->remove_freeLBNList(sp_ptr_->mapping_store);
        lbnPool_->remove_freeLBNList(sp_ptr_->log_store);
        lbnPool_->remove_freeLBNList(SUPER_BLOCK);
        rebuild_super_page();
    } 
    else {
        pr_info("Super page magic number is correct, initializing IMS interface");

        *sp_ptr_ = *sp;
        *sp_ptr_ = *sp;
        lbnPool_->set_lastUsedChannel(sp_ptr_->lastUsedChannel);
        sp_ptr_->dump();
        err = mappingTable_->init_mapping_table(sp_ptr_->mapping_store, sp_ptr_->mapping_page_num);
        if (err != OPERATION_SUCCESS) {
            pr_error("Initialize mapping table failed");
            free(buffer);
            return OPERATION_FAILURE;
        }
        lsmTree_->rebuild_level_counts();
        lsmTree_->debug_check_level_counts("device-init_IMS-after-mapping-recovery");
        pr_info("InitLogRecordList: logStoreLBN = %lu", sp_ptr_->log_store);
        err = logManager_->init_logRecordList(sp_ptr_->log_store, sp_ptr_->log_page_num);
        if (err != OPERATION_SUCCESS) {
            pr_error("Initialize log record list failed");
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
        lbnPool_->remove_freeLBNList(sp_ptr_->mapping_store);
        lbnPool_->remove_freeLBNList(sp_ptr_->log_store);
        lbnPool_->remove_freeLBNList(SUPER_BLOCK);
    }
    
    if ( (usedLBN) != sp_ptr_->usedLBN_num) {
        pr_error("Initialize LBN pool failed");
        free(buffer);
        return OPERATION_FAILURE;
    }
    logManager_->currentLogLBN = sp_ptr_->currentLogLBN;
    logManager_->nextLogLBN = sp_ptr_->nextLogLBN;
    logManager_->logOffset = sp_ptr_->logOffset;

    free(buffer);
    pr_info("Initialize LBN pool success");
    pr_info("Init_IMS is done");    
    dump_all();
    return OPERATION_SUCCESS;
}

int IMS_interface::close_IMS() {
    // if (closed_) {
    //     pr_info("IMS_interface already closed, skip close_IMS");
    //     return OPERATION_SUCCESS;
    // }
    int err = OPERATION_FAILURE;
    pr_info("Close IMS interface");
    auto mappingTable = mappingTable_->get_table();
    err = persistenceManager_->flushMappingTable(mappingTable);
    if (err != OPERATION_SUCCESS) {
        pr_error("Flushing mapping table to disk failed");
        return OPERATION_FAILURE;
    }

    err = logManager_->flush_logRecordList();
    if (err != OPERATION_SUCCESS) {
        pr_error("Flushing log record list to disk failed");
        return OPERATION_FAILURE;
    }

    uint8_t* buffer = (uint8_t*)malloc(IMS_PAGE_SIZE);
    if (!buffer) {
        pr_error("Allocating memory for super page buffer failed");
        return OPERATION_FAILURE;
    }

    memset(buffer, 0, IMS_PAGE_SIZE);
    super_page* sp = (super_page*)buffer;

    sp->magic = MAGIC;
    
    sp->mapping_page_num = sp_ptr_->mapping_page_num;
    sp->log_page_num     = sp_ptr_->log_page_num;
    sp->currentLogLBN    = logManager_->currentLogLBN;
    sp->nextLogLBN       = logManager_->nextLogLBN;
    // sp->logOffset        = logManager_->logOffset;

    sp->log_store           = sp_ptr_->log_store;
    sp->mapping_store       = sp_ptr_->mapping_store;

    sp->global_sequence     = sp_ptr_->global_sequence;
    sp->sstable_sequence    = sp_ptr_->sstable_sequence;
    sp->logOffset           = sp_ptr_->logOffset;
    sp->byteOffset          = sp_ptr_->byteOffset;
    sp->firstBlockOffset    = sp_ptr_->firstBlockOffset;


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

    // err = disk_.writePage(0, buffer);
#if RUNTYPE == 1
    // SimpleSSD 環境，用指標
    err = disk_->writePage(0, buffer);
#else
    // host / my env，用物件
    err = disk_.writePage(0, buffer);
#endif

    if (err != OPERATION_SUCCESS) {
        pr_error("Writing super page to disk failed");
        free(buffer);
        return OPERATION_FAILURE;
    }

    // free IMS buffer
    // free_device_buffer();
    free(buffer);
    // lsmTree_->clear();             
    // lbnPool_->clear();             
    // mappingTable_->clear();      
    // logManager_->clear();        
    // reset_superPage(sp_ptr_);
    // closed_ = true;
    return OPERATION_SUCCESS;
}
int IMS_interface::init_DB(uint8_t *buffer){
    uint32_t next_lbn = get_logManager()->nextLogLBN;
    uint32_t current_lbn = get_logManager()->currentLogLBN;
    uint32_t page_offset = get_logManager()->logOffset;
    
}
// TODO now not complete still need to modify
int IMS_interface::write_log(uint64_t lpn,uint8_t *buffer){
    pr_debug("Write log at LPN: %lu in IMS", lpn);
    if (buffer == nullptr) {
        pr_error("Read log failed: null request or buffer");
        return OPERATION_FAILURE;
    }
    int err = OPERATION_SUCCESS;
    if(ENABLE_DISK){
        err = get_persistenceManager()->writePage(lpn,buffer,IMS_PAGE_SIZE);
    }
    
    if( err != OPERATION_SUCCESS){
        pr_error("Write value log failed at LPN %lu", lpn);
    }
    return err;
}

int IMS_interface::read_log(uint64_t lpn,uint8_t *buffer){
    if (buffer == nullptr) {
        pr_error("Read log failed: null request or buffer");
        return OPERATION_FAILURE;
    }
    int err = OPERATION_SUCCESS;
    err = get_persistenceManager()->readPage(lpn,buffer,IMS_PAGE_SIZE);
    if( err != OPERATION_SUCCESS){
        pr_error("Read value log failed at LPN %lu", lpn);
    }
    return err;
}

int IMS_interface::write_block(uint32_t lbn, uint8_t* buffer){
    int err = OPERATION_FAILURE;
    if (buffer == nullptr) {
        pr_error("Read log failed: null request or buffer");
        return err;
    }
    if (lbn > LBN_NUM) {
        pr_error("LBN is out of range");
        return err;
    }
    err = get_persistenceManager()->writeBlock(lbn,buffer,BLOCK_SIZE);
    if( err != OPERATION_SUCCESS){
        pr_error("Write block failed at LBN %lu", lbn); err;
    }
    return err;
}


int IMS_interface::read_block(uint32_t lbn, uint8_t* buffer){
    int err = OPERATION_FAILURE;
    if (buffer == nullptr) {
        pr_error("Read log failed: null request or buffer");
        return err;
    }
    if (lbn > LBN_NUM) {
        pr_error("LBN is out of range");
        return err;
    }
    err = get_persistenceManager()->readBlock(lbn,buffer,BLOCK_SIZE);
    if( err != OPERATION_SUCCESS){
        pr_error("Read block failed at LBN %lu", lbn);
    }
    return err;
}



void IMS_interface::reset_superPage(super_page *sp) {
    if (sp == nullptr) {
        pr_error("Super page pointer is null, cannot reset");
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
    // int err = disk_.writePage(0, buffer);



#if RUNTYPE == 1
    int err = disk_->writePage(0, buffer);
#else
    int err = disk_.writePage(0, buffer);
#endif
    if (err != OPERATION_SUCCESS) {
        pr_error("Writing super page to disk failed");
        free(buffer);
        return OPERATION_FAILURE;
    }
    return OPERATION_SUCCESS;
}

int IMS_interface::dump_IMS(){
    dump_all();
    return OPERATION_SUCCESS;
}

int IMS_interface::open_DB(uint32_t *datalen) {
    std::string result;
    DB_INIT info;
    // init_IMS();
    info.current_lbn  = get_logManager()->currentLogLBN;
    info.next_lbn     = get_logManager()->nextLogLBN;
    info.page_offset  = get_superPage()->logOffset;
    info.byte_offset  = get_superPage()->byteOffset;
    info.first_block_offset = get_superPage()->firstBlockOffset;
    info.global_seq   = get_superPage()->global_sequence;
    info.sstable_seq  = get_superPage()->sstable_sequence;
    info.log_list     = get_logManager()->encode();
    info.node_list    = get_lsmTree()->encode();


    // info.dump();
    result = info.encode();
    pr_info("Device DRAM space info, result.size=%zu, buffer_size_=%zu",result.size(), buffer_size_);
    if (result.size() > buffer_size_){
        pr_error("Device DRAM space is not enough,result.size=%zu, buffer_size_=%zu",result.size(), buffer_size_);
        return OPERATION_FAILURE;
    }
    std::lock_guard<std::mutex> lk(buf_mu_);
    std::memcpy(buffer_, result.data(), result.size());
    buffer_valid_size_ = result.size();
    *datalen = result.size();
    return OPERATION_SUCCESS;
}

int IMS_interface::close_DB(uint8_t *host_buffer, size_t size){
    pr_info("Database is closing ,dump dabase information");
    dump_all();
    if (!host_buffer || size == 0) return -2;
    if (!sp_ptr_) {
        pr_error("close_DB: super_page(old) not initialized");
        return -5;
    }
    DB_INIT info;
    if( DB_INIT::decode(std::string(reinterpret_cast<char*>(host_buffer), size),info) == false){
        pr_error("DB_INIT decode failed");
        return -6;
    }

    sp_ptr_->global_sequence = info.global_seq;
    sp_ptr_->sstable_sequence = info.sstable_seq;
    sp_ptr_->logOffset = static_cast<uint64_t>(info.page_offset);
    sp_ptr_->byteOffset = static_cast<uint64_t>(info.byte_offset);
    sp_ptr_->firstBlockOffset = static_cast<uint64_t>(info.first_block_offset);
#if RUNTYPE == 1
    int err = close_IMS();
    if(err == OPERATION_FAILURE){
        pr_error("close IMS failed");
    }
#endif
    return 0;
}

int IMS_interface::set_sstable_info(uint32_t *size){
    std::string sstable_enc = lsmTree_->encode();
    uint32_t enc_size = sstable_enc.size();
    if (enc_size > buffer_size_) {
        return OPERATION_FAILURE;
    }
    {
        std::lock_guard<std::mutex> lk(buf_mu_);
        memcpy(buffer_, sstable_enc.data(), enc_size);
        buffer_valid_size_ = enc_size;
    }
    *size = enc_size;
    return OPERATION_SUCCESS;
}
int IMS_interface::set_log_info(uint32_t *size){
    std::string log_enc = logManager_->encode();
    uint32_t enc_size = log_enc.size();

    if (enc_size > buffer_size_) {
        return OPERATION_FAILURE;
    }
    {
        std::lock_guard<std::mutex> lk(buf_mu_);
        memcpy(buffer_, log_enc.data(), enc_size);
        buffer_valid_size_ = enc_size;
    }

    *size = enc_size;
    return OPERATION_SUCCESS;
}

int IMS_interface::search(std::vector<uint64_t> &pbn_list){
    std::vector<uint32_t> ch_list(CHANNEL_NUM,0);
    if(ch_list.size() > CHANNEL_NUM){
        pr_error("Channel list size is error");
        return OPERATION_FAILURE;
    }
    if (buffer_ == nullptr || buffer_valid_size_ == 0) {
        pr_error("search: buffer_ is null or buffer_valid_size_ == 0");
        return OPERATION_FAILURE;
    }
    size_t hostInfo_len = buffer_valid_size_;
    std::string buf(buffer_, buffer_ + hostInfo_len);
#if (SEARCH_PATTERN == 0)
    SearchPackageD search_package;
    if (!SearchPackageD::decode(buf, search_package)) {
        pr_error("IMS search: decode SearchPackageD failed");
        return  OPERATION_FAILURE;
    }
#elif (SEARCH_PATTERN == 1)
    SearchPackageH search_package;
    if (!SearchPackageH::decode(buf, search_package)) {
        pr_error("IMS search: decode SearchPackageD failed");
        return  OPERATION_FAILURE;
    }
#endif
    for(auto& pattern : search_package.searchPatterns){
        auto& sstable_ID = pattern.sstable_name;
        uint64_t lbn = mappingTable_->getLBN(sstable_ID);
        if(lbn == INVALIDLBN){
            continue;
        }
        pbn_list.push_back(lbn);
        auto ch = LBN2CH(lbn);
        if (ch < 0 || ch >= CHANNEL_NUM) {
            pr_error("IMS search: invalid channel %d for sstable %s", ch, sstable_ID.c_str());
            continue;
        }
        ch_list[ch]++;
    }
    auto it = std::max_element(ch_list.begin(),ch_list.end());
    
    if (it != ch_list.end()) {
        // pr_info("Search block num in parllel:%u",*it);
        if(*it > 2){
            pr_debug("This search run is exceed 2 ,is %d",*it);
            for(auto& pattern : search_package.searchPatterns){
                auto& sstable_ID = pattern.sstable_name;
                uint64_t lbn = mappingTable_->getLBN(sstable_ID);
                if(lbn == INVALIDLBN){
                    continue;
                }
                pr_debug("Search SStable_ID:%s in CH[%d]",sstable_ID.c_str(),LBN2CH(lbn));
            }
        }
        total_search_parallel_block_num += *it;
    }
    total_search_count++;
    return OPERATION_SUCCESS;
}

int IMS_interface::simulate_compaction_io(std::vector<uint64_t> &lbn_list) {
    std::vector<uint32_t> ch_list(CHANNEL_NUM, 0);

    size_t len = buffer_valid_size_;
    if (len == 0) {
        pr_error("simulate_compaction_io: buffer_valid_size_ == 0");
        return OPERATION_FAILURE;
    }

    CompactionIOSimMeta meta =
        CompactionIOSimMeta::decode(reinterpret_cast<char*>(buffer_), len);

    pr_debug("Simulate compaction IO: src=%zu dst=%zu",
             meta.src_files.size(), meta.dst_files.size());

    auto handle_one_side = [&](const std::vector<std::string>& files,
                               const char* tag) {
        for (const auto& sstable_ID : files) {
            uint64_t lbn = mappingTable_->getLBN(sstable_ID);
            if (lbn == INVALIDLBN) {
                pr_error("simulate_compaction_io %s: no mapping for %s",
                         tag, sstable_ID.c_str());
                continue;
            }

            lbn_list.push_back(lbn);

            int ch = LBN2CH(lbn);
            if (ch < 0 || ch >= CHANNEL_NUM) {
                pr_error("simulate_compaction_io %s: invalid channel %d for %s",
                         tag, ch, sstable_ID.c_str());
                continue;
            }
            ch_list[ch]++;
        }
    };

    handle_one_side(meta.src_files, "src");
    handle_one_side(meta.dst_files, "dst");
    total_compaction_count++;
    auto it = std::max_element(ch_list.begin(), ch_list.end());
    if (it != ch_list.end()) {
        total_compaction_parallel_block_num += *it;
    }
    
    if (it != ch_list.end()) {
        uint32_t max_load = *it;
        int max_ch = static_cast<int>(std::distance(ch_list.begin(), it));
        total_compaction_parallel_block_num += max_load;

        std::ostringstream oss;
        oss << "Channel matrix:[";
        for (int i = 0; i < CHANNEL_NUM; i++) {
            if (i) oss << ",";
            oss << ch_list[i];
        }
        oss << "]";

        pr_stat("%s", oss.str().c_str());
        pr_stat("The max load channel is %d, load is %u", max_ch, max_load);
    } else {
        pr_stat("Channel matrix:[]");
        pr_stat("No channel load recorded");
    }
    return OPERATION_SUCCESS;
}



void IMS_interface::print_result(){
    std::vector<uint32_t> ch_info = get_lbnpool()->get_channel_info();
    if(ch_info.empty() || ch_info.size() != CHANNEL_NUM){
        pr_error("Channel info size is error");
    }
    pr_stat("================= IMS experient result =================");
    for(int i = 0;i < CHANNEL_NUM;i++){
        pr_stat("waer leveling CH[%d]=%u",i,ch_info[i]);
    }
    pr_stat("================= SStable count per CH =================");
    for(int i = 0;i < CHANNEL_NUM;i++){
        pr_stat("SStable write count CH[%d]=%u",i,sstable_count_per_ch[i]);
    }
    pr_stat("=========================================================");
    pr_stat("Total SStable write count %u",total_sstable_write_count);
    // pr_stat("inter=%f (Impact on search performance)",alpha_inter_);
    // pr_stat("intra=%f (Impact on compaction performance)",alpha_intra_);
    pr_stat("Total compaction_parallel_block_num =%u",total_compaction_parallel_block_num);
    pr_stat("Total compaction count=%u",total_compaction_count);
    double block_per_compaction = (double)(total_compaction_parallel_block_num) / (double)(total_compaction_count);
    pr_stat("Avg. compaction parallel block per compaction =%04f",block_per_compaction);
    pr_stat("Total_search_parallel_block_num=%u ",total_search_parallel_block_num);
    pr_stat("Total_search_count=%u",total_search_count);
    double block_per_search = (double)(total_search_parallel_block_num) / (double)(total_search_count);
    pr_stat("Avg. search parallel block per search =%04f",block_per_search);
    pr_stat("================= IMS experient end =================");
}


int IMS_interface::trivial_move(){
    int err = OPERATION_SUCCESS;
    if (buffer_ == nullptr || buffer_valid_size_ == 0) {
        pr_error("read_sstable: buffer_ is null or buffer_valid_size_ == 0");
        return OPERATION_FAILURE;
    }
    size_t hostInfo_len = buffer_valid_size_;
    std::string buf(buffer_, buffer_ + hostInfo_len);
    hostInfo request = hostInfo::decodeOrThrow(buf);
    std::string filename = request.filename;

    auto lsmtree = get_lsmTree();
    auto node = lsmtree->find_node(filename);
    if(node == nullptr){
        pr_error("Can't find the SStable(%s) in LSM-tree",filename.c_str());
        return OPERATION_FAILURE;
    }
    if( compareKey(node->rangeMin,request.rangeMin) != 0 ||
        compareKey(node->rangeMax,request.rangeMax) != 0)
    {
        pr_error("SStable(%s) key range is mismatch whitch is in LSM-tree",filename.c_str());
        pr_error("The key range in host: %s ~ %s",request.rangeMin.toString().c_str(),request.rangeMax.toString().c_str());
        pr_error("The key range in device: %s ~ %s",node->rangeMin.toString().c_str(),node->rangeMax.toString().c_str());
        return OPERATION_FAILURE;
    }

    if (request.levelInfo < 0 || request.levelInfo >= MAX_LEVEL) {
        pr_error("trivial_move target level is invalid: %d", request.levelInfo);
        return OPERATION_FAILURE;
    }

    if (node->levelInfo == request.levelInfo) {
        pr_debug("trivial_move no-op for %s at level %d",
                 filename.c_str(), request.levelInfo);
        return OPERATION_SUCCESS;
    }

    auto existed_dst = lsmtree->find_node(filename,
                                          request.levelInfo,
                                          node->rangeMin,
                                          node->rangeMax);
    if (existed_dst) {
        pr_error("trivial_move destination node already exists: %s", filename.c_str());
        return OPERATION_FAILURE;
    }

    auto moved = std::make_shared<TreeNode>(filename,
                                            request.levelInfo,
                                            node->channelInfo,
                                            node->rangeMin,
                                            node->rangeMax);

    lsmtree->remove_sstable(node);
    lsmtree->insert_sstable(moved);
    return err;
}