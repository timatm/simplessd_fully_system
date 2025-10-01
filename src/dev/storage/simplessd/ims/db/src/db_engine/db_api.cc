#include "db_api.hh"
#include "nvme_interface.hh"
#include "nvme_test.hh"
#include "IMS_interface.hh"
#include "lsmtree.hh"
#include "options.hh"
#include "compaction.hh"
#include "range_query.hh"
API::API(){
    tree_ = std::make_shared<Tree>();
    lsmTree_ = std::make_unique<LSMTree>(tree_);
    if(NVME_DRIVER == 1){

    }
    else{
        nvme_ = std::make_unique<MyNVMeDriver>();
    }
    
    packing_ = PACKING_T;
    memtable_ = std::make_unique<MemTable>();
    immutable_memtable_ = nullptr;
    logManager_ = std::make_unique<LogManager>(*nvme_);
    global_seq_ = 0;
    sstableManager_ = std::make_unique<SstableManager>(*nvme_,*lsmTree_);
    compaction_key_list_.resize(MAX_LEVEL);

    sstableManager_->set_on_write_done([this](const sstable_info& info) {
        this->OnSSTableFlushed(info);
    });
    sstableManager_->set_on_write_fail([this](const sstable_info& info, int err) {
        this->OnSSTableWriteFailed(info, err);
    });
    keyRangeCache_ = std::make_unique<ReadCache>();

}

Status API::open() {
    pr_info("Opening database...");

    void* buffer = aligned_alloc(4096, IMS_PAGE_SIZE);
    if (!buffer) {
        return Status::IOError("Failed to allocate buffer for open operation");
    }

    std::memset(buffer, 0, IMS_PAGE_SIZE);

    int err = nvme_->nvme_open_DB(reinterpret_cast<uint8_t*>(buffer));
    if (err != 0) {
        free(buffer);
        return Status::IOError("nvme_open_DB failed");
    }

    std::string buf(reinterpret_cast<char*>(buffer), IMS_PAGE_SIZE);
    free(buffer);

    DB_INIT info;
    if(DB_INIT::decode(buf, info) == false){
        return Status::Corruption("DB_INIT decode failed");
    }      
    
    pr_debug("open DB info");
    // info.dump();

    getLogManager()->setNextLBN(info.next_lbn);
    getLogManager()->setCurrentLBN(info.current_lbn);
    getLogManager()->setPageOffset(info.page_offset);
    getLogManager()->setByteOffset(info.byte_offset);
    getLogManager()->setFirstBlockOffset(info.first_block_offset);
    global_seq_ = info.global_seq;
    getSSTable()->setSequenceNumber(info.sstable_seq);

    if (!getLogManager()->decode(info.log_list)) {
        return Status::Corruption("LogManager decode failed");
    }

    if (!getLSMTree()->decode(info.node_list)) {
        return Status::Corruption("LSMTree decode failed");
    }

    return Status::OK();
}

Status API::close(){
    sstableManager_->waitAllTasksDone();
    pr_info("Closing database  .......");

    // 等待過去已經排入的 flush/compaction 都完成
    
    std::shared_ptr<MemTable> imm_to_flush;

    {
        std::lock_guard<std::mutex> lk(mu_);
        if (memtable_ && !memtable_->isEmpty()) {
            // 关键：先把 unique_ptr 所有权转给 shared_ptr（C++14 起支持）
            imm_to_flush = std::shared_ptr<MemTable>(std::move(memtable_));
            // // 现在 memtable_ 已经变成空了，不会析构原对象
            // memtable_ = std::make_unique<MemTable>();  // 新建一张空表供后续写入
        }
    }

    // 离开锁后再做这些 I/O/序列化操作，降低锁粒度
    if (imm_to_flush) {
        auto minK = imm_to_flush->getMinKey();
        auto maxK = imm_to_flush->getMaxKey();
        const auto& list_ref = imm_to_flush->GetSkipList();
        auto buffer = sstableManager_->packingTable(list_ref);
        if (!buffer.data() || buffer.size == 0) {
            return Status::IOError("Packing failed");
        }
        sstableManager_->writeSSTable(0, minK, maxK, std::move(buffer), /*clearImmutableTable=*/true);
    }
    sstableManager_->waitAllTasksDone();

    uint32_t page_offset = getLogManager()->get_page_offset();
    uint32_t byte_offset = getLogManager()->get_byte_offset();
    uint32_t first_block_offset = getLogManager()->get_first_block_offset();
    logManager_->flush_buffer();

    DB_INIT info;
    info.page_offset = page_offset;
    info.byte_offset = byte_offset;
    info.first_block_offset = first_block_offset;
    info.sstable_seq = getSSTable()->getSequenceNumber();
    info.global_seq = global_seq_;
    std::string enc_info = info.encode();
    int err = nvme_->nvme_close_DB(reinterpret_cast<uint8_t*>(enc_info.data()), enc_info.size());   
    
    if (err != OPERATION_SUCCESS) {
        return Status::IOError("nvme_close_DB failed");
    }
    lsmTree_->clear();
    return Status::OK();
}


Status API::put(std::string key, std::string value) {
    return put_impl(std::move(key), std::move(value), PutType::kPutByUser);
}

Status API::put_from_gc(std::string key, std::string value) {
    return put_impl(std::move(key), std::move(value), PutType::kPutByGC);
}


Status API::put_impl(std::string key ,std::string value,PutType t){
    std::shared_ptr<MemTable> imm_hold;
    InternalKey minK, maxK;
    bool need_flush = false;

    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!memtable_) memtable_ = std::make_unique<MemTable>();
        if (memtable_->memTableIsFull()) {
            imm_hold = std::shared_ptr<MemTable>(std::move(memtable_));
            immutable_memtable_ = imm_hold;              
            memtable_ = std::make_unique<MemTable>();

            minK = imm_hold->getMinKey();
            maxK = imm_hold->getMaxKey();
            need_flush = true;
        }
    }

    if (need_flush) {
        const auto& list_ref = imm_hold->GetSkipList();  // 注意：是 const&，不是 shared_ptr
        auto buffer = sstableManager_->packingTable(list_ref);
        if ( buffer.data() == nullptr || buffer.size != BLOCK_SIZE) return Status::IOError("Packing failed");
        sstableManager_->writeSSTable(0, minK, maxK, std::move(buffer), /*clearImmuteTable=*/true);
    }
    compaction();
    uint32_t lpn = 0;
    uint32_t offset = 0;
    getLogManager()->getLPN(lpn, offset);
    uint64_t seq = global_seq_.fetch_add(1); 
    InternalKey internal_key(key,lpn,offset,seq,ValueType::kTypeValue);
    Record internal_value(internal_key,value);
    logManager_->writeLog(internal_value);
    memtable_->Put(internal_value);
    if(logManager_->get_log_block_num() >= LOG_GC_THRESHOLD && t == PutType::kPutByUser){
        log_garbage_collection();
    }
    return Status::OK();
}


Status API::delete_key(std::string key ,std::string value){
   std::shared_ptr<MemTable> imm_hold;
    InternalKey minK, maxK;
    bool need_flush = false;

    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!memtable_) memtable_ = std::make_unique<MemTable>();
        if (memtable_->memTableIsFull()) {
            // 把 unique_ptr 轉成 shared_ptr
            imm_hold = std::shared_ptr<MemTable>(std::move(memtable_));
            immutable_memtable_ = imm_hold;              // 讓讀者可見
            memtable_ = std::make_unique<MemTable>();    // 新 memtable

            minK = imm_hold->getMinKey();                // by value
            maxK = imm_hold->getMaxKey();
            need_flush = true;
        }
    } // 解鎖

    if (need_flush) {
        const auto& list_ref = imm_hold->GetSkipList();  // 注意：是 const&，不是 shared_ptr
        auto buffer = sstableManager_->packingTable(list_ref);
        if ( buffer.data() == nullptr || buffer.size != BLOCK_SIZE) return Status::IOError("Packing failed");
        sstableManager_->writeSSTable(0, minK, maxK, std::move(buffer), /*clearImmuteTable=*/true);
    }
    compaction();
    uint32_t lpn = 0;
    uint32_t offset = 0;
    getLogManager()->getLPN(lpn, offset);
    uint64_t seq = global_seq_.fetch_add(1); 
    InternalKey internal_key(key,lpn,offset,seq,ValueType::kTypeDeletion);
    Record internal_value(internal_key,value);
    logManager_->writeLog(internal_value);
    memtable_->Put(internal_value);
    return Status::OK();
}

Status API::get(std::string key,std::string& value){
    if(key.empty()){
        return Status::IOError("Key string is empty");
    }
    // std::cout << "Search key: " << key << std::endl;
    Key interkey(key);
    InternalKey search_key(key);
    auto result = std::optional<std::string>{};
    if(memtable_){
        result = memtable_->Get(key);
    }
    
    if(!result.has_value() && immutable_memtable_){
        result = immutable_memtable_->Get(key);
    }
    int level = 0;
    if(!result.has_value()){
        auto sstables = lsmTree_->search_key(interkey);
        char * buffer = (char *)allocateAligned(BLOCK_SIZE);
        while(!result.has_value() && !sstables.empty()){
            auto sstable = sstables.front();
            // std::cout   << "Find SStable: " << sstable->filename << "  Key range [ " << sstable->rangeMin.toString() << " ~ "
            //             << sstable->rangeMax.toString() << " ]" <<std::endl;
            sstables.pop();
            sstableManager_->readSSTable(sstable->filename,buffer);
            getSSTable()->waitAllTasksDone();
            auto keys = parse_sstable(buffer);
            auto it = keys.find(search_key);
            if(it != keys.end()){
                // it->dump();
                uint32_t lpn = it->value_ptr.lpn;
                uint32_t offset = it->value_ptr.offset;
                if(it->info.type == static_cast<uint8_t>(ValueType::kTypeDeletion) ){
                    std::cout << "Key is not found ,becasue this key has been deleted" << std::endl;
                    free(buffer);
                    return Status::NotFound("The key has been deleted");
                }
                auto record = logManager_->readLog(lpn, offset);
                // record->Dump();
                if(record.has_value()){
                    result = (*record).value;
                }
                else{
                    pr_debug("Failed to read log for key: %s at LPN: %u, offset: %u", key.c_str(), lpn, offset);
                    free(buffer);
                    return Status::IOError("Failed to read log for key");
                }
                
            }
        }
        free(buffer);
    }
    if(!result.has_value()){
        return Status::NotFound("The key isn't in the DB");
    }
    value = result.value();
    return Status::OK();
}



Status API::get(std::string key,Record& rec){
    if(key.empty()){
        return Status::IOError("Key string is empty");
    }
    // std::cout << "Search key: " << key << std::endl;
    Key interkey(key);
    InternalKey search_key(key);
    auto result = memtable_->get_record(key);
    if(!result.has_value() && immutable_memtable_){
        result = immutable_memtable_->get_record(key);
    }
    int level = 0;
    if(!result.has_value()){
        auto sstables = lsmTree_->search_key(interkey);
        char * buffer = (char *)allocateAligned(BLOCK_SIZE);
        while(!result.has_value() && !sstables.empty()){
            auto sstable = sstables.front();
            // std::cout   << "Find SStable: " << sstable->filename << "  Key range [ " << sstable->rangeMin.toString() << " ~ "
            //             << sstable->rangeMax.toString() << " ]" <<std::endl;
            sstables.pop();
            sstableManager_->readSSTable(sstable->filename,buffer);
            getSSTable()->waitAllTasksDone();
            auto keys = parse_sstable(buffer);
            auto it = keys.find(search_key);
            if(it != keys.end()){
                // it->dump();
                uint32_t lpn = it->value_ptr.lpn;
                uint32_t offset = it->value_ptr.offset;
                if(it->info.type == static_cast<uint8_t>(ValueType::kTypeDeletion) ){
                    std::cout << "Key is not found ,becasue this key has been deleted" << std::endl;
                    free(buffer);
                    return Status::NotFound("The key has been deleted");
                }
                auto record = logManager_->readLog(lpn, offset);
                // record->Dump();
                if(record.has_value()){
                    result = record;
                }
                else{
                    pr_debug("Failed to read log for key: %s at LPN: %u, offset: %u", key.c_str(), lpn, offset);
                    free(buffer);
                    return Status::IOError("Failed to read log for key");
                }
                
            }
        }
        free(buffer);
    }
    if(!result.has_value()){
        return Status::NotFound("The key isn't in the DB");
    }
    rec = result.value();
    return Status::OK();
}

std::set<InternalKey ,SetComparator> API::parse_sstable(char* buffer) {
    size_t offset = 0;
    std::set<InternalKey ,SetComparator> keys;

    while (offset + sizeof(InternalKey) <= BLOCK_SIZE) {
        InternalKey key;
        key = InternalKey::Decode( std::string((buffer + offset) ,sizeof(InternalKey)) );
        offset += sizeof(InternalKey);
        if(key.IsValid()){
            keys.insert(key);
        }
    }

    return keys;
}

void API::dump_system() {
    std::cout << "Dumping system information..." << std::endl;
    std::cout << "SSD config:" << std::endl;
    std::cout   << "channel num: " << CHANNEL_NUM
                << ", plane num: " << PLANE_NUM
                << ", die num: " << DIE_NUM
                << ", package num: " << PACKAGE_NUM
                << ", block num: " << BLOCK_NUM
                << ", page num: " << IMS_PAGE_NUM
                << ", page size: " << IMS_PAGE_SIZE
                << std::endl;
    std::cout << "DB config:" << std::endl;
    std::cout << "Global sequence number: " << global_seq_.load() << std::endl;
    std::cout << "SSD simulator type:" << ( NVME_DRIVER == 0 ? "My sim" : "SimpleSSD sim" )<< std::endl;
    std::cout << "SStable packing strategy: " << std::endl;
}


void API::dump_memtable() {
    if (memtable_) {
        memtable_->Dump();
    } else {
        std::cout << "MemTable is empty." << std::endl;
    }
}

void API::dump_lsmtree(){
    if(sstableManager_){
        sstableManager_->dump();
    } else {
        std::cout << "SSTable Manager is not initialized." << std::endl;
    }
};

void API::dump_log_manager(){
    if(logManager_) {
        logManager_->dump();
    } else {
        std::cout << "Log Manager is not initialized." << std::endl;
    }
}

void API::dump_all(){
    dump_memtable();
    dump_lsmtree();
    dump_log_manager();
    std::cout << "All components dumped successfully." << std::endl;
};




std::set<std::string> API::read_key_range(const std::string& filename){
    if(filename.empty()){
        throw std::invalid_argument("Filename cannot be empty");
    }
    if(getLSMTree()->find_node(filename) == nullptr){
        throw std::runtime_error("SSTable not found in LSMTree: " + filename);
    }
    char* buffer = static_cast<char*>(std::aligned_alloc(4096,IMS_PAGE_SIZE));
    if (!buffer) {
        throw std::runtime_error("Failed to allocate buffer for reading key range");
    }
    int err = nvme_->nvme_read_ssKeyRange(filename, buffer);
    if(err == COMMAND_FAILED){
        free(buffer);
        throw std::runtime_error("Failed to read SSTable: " + filename);
    }
    auto keys = parse_sstable_page(buffer);
    std::set<std::string> key_set;
    for(const auto& key : keys){
        key_set.insert(key.UserKey());
    }
    free(buffer);
    return key_set;
}


SearchPatternD API::generate_SearchPatternD(const std::string& filename, const Key& searchKey,const std::set<std::string>& keys){
    if(filename.empty()){
        throw std::invalid_argument("Filename cannot be empty");
    }
    if (filename.size() != 35) {
        throw std::invalid_argument("sstable_name must be 35 bytes");
    }
    if(keys.empty()){
        throw std::invalid_argument("Keys set cannot be empty");
    }
    if(searchKey.key_size == 0){
        throw std::invalid_argument("Search key cannot be empty");
    }
    auto it = keys.upper_bound(searchKey.toString());
    if(it == keys.begin()){
        throw std::out_of_range("No predecessor: provided key is smaller than the smallest key");
    }
    SearchPatternD pattern;
    pattern.slot_index = static_cast<uint32_t>(std::distance(keys.begin(), std::prev(it)));
    pattern.sstable_name = filename;
    return pattern;
}

SearchPatternH API::generate_SearchPatternH(const std::string& filename,const Key& searchKey,const std::set<std::string>& keys){
    if (filename.empty()) throw std::invalid_argument("Filename cannot be empty");
    constexpr std::size_t kSSTNameLen = 35;
    if (filename.size() != kSSTNameLen) throw std::invalid_argument("sstable_name must be 36 bytes");
    if (keys.empty()) throw std::invalid_argument("Keys set cannot be empty");
    if (searchKey.key_size == 0) throw std::invalid_argument("Search key cannot be empty");

    auto it = keys.upper_bound(searchKey.toString());
    if (it == keys.begin()) {
        throw std::out_of_range("No predecessor: provided key is smaller than the smallest key");
    }
    const size_t slot_index = static_cast<size_t>(std::distance(keys.begin(), std::prev(it)));

    const std::string enc = searchKey.encode();
    if (enc.empty()) {
        throw std::invalid_argument("Encoded search key is empty");
    }

   
    const size_t num_slots = IMS_PAGE_SIZE / SLOT_SIZE;
    if (num_slots == 0 || IMS_PAGE_SIZE % SLOT_SIZE != 0) {
        throw std::logic_error("Invalid IMS_PAGE_SIZE / SLOT_SIZE configuration");
    }
    if (slot_index >= num_slots) {
        throw std::out_of_range("slot_index is out of range for one page");
    }
    if (enc.size() > SLOT_SIZE) {
        throw std::length_error("Encoded key doesn't fit into one SLOT");
    }
    std::string search_pattern(IMS_PAGE_SIZE, static_cast<char>(0xFF));
    std::memcpy(search_pattern.data() + slot_index * SLOT_SIZE, enc.data(), enc.size());

    SearchPatternH pattern;
    pattern.sstable_name = filename;
    pattern.search_pattern = std::move(search_pattern);
    return pattern;
}


std::set<InternalKey ,SetComparator> API::parse_sstable_page(char* buffer) {
    size_t offset = 0;
    std::set<InternalKey ,SetComparator> keys;

    while (offset + sizeof(InternalKey) <= IMS_PAGE_SIZE) {
        InternalKey key;
        // std::string buf(buffer ,BLOCK_SIZE);
        key = InternalKey::Decode( (buffer + offset));
        offset += sizeof(InternalKey);
        if(key.info.type == INVALID_KEY_TYPE){
            continue;
        }
        keys.insert(key);
    }

    return keys;
}


#if (SEARCH_PATTERN == 0)
Status API::search(std::string key ,std::string& value){
    if(key.empty()){
        return Status::IOError("Key string is empty");
    }
    std::cout << "Search key: " << key << std::endl;
    Key userKey(key);
    InternalKey internalKey(key);
    if (memtable_) {
        auto result = memtable_->Get(key);
        if (!result.has_value() && immutable_memtable_) {
            result = immutable_memtable_->Get(key);
        }
        if (result.has_value()) {
            Record rec = Record::Decode(*result);
            if(rec.internal_key.info.type == static_cast<uint8_t>(ValueType::kTypeDeletion)){
                return Status::NotFound("The key has been deleted");
            }
            value = rec.value;
            return Status::OK();
        }
    }

    auto sstables = lsmTree_->search_key(userKey);
    if (sstables.empty()){
        pr_info("No candidate SSTables found for key: %s", key.c_str());
        return Status::OK();
    }
    SearchPackageD search_package;


    while( !sstables.empty() ){
        auto sstable = sstables.front();
        std::cout   << "Find SStable: " << sstable->filename << "  Key range [ " << sstable->rangeMin.toString() << " ~ "
                    << sstable->rangeMax.toString() << " ]" <<std::endl;
        sstables.pop();
        SearchPatternD pattern_info;
        switch (packing_){
            case PackingType::kKeyPerPage:{
                pattern_info.slot_index = 0;
                break;
            }
            case PackingType::kHash:{
                pattern_info.slot_index = HashModN(internalKey, SLOT_NUM_PER_PAGE); 
                break;
            }
                
            case PackingType::kKeyRange:{
                auto key_range = keyRangeCache_->get(sstable->filename);
                if(key_range == std::nullopt){
                    key_range = read_key_range(sstable->filename);
                    if(!key_range.has_value()){
                        return Status::NotFound("Key not found in SSTable: " + sstable->filename);
                    }
                    keyRangeCache_->put(sstable->filename, *key_range);
                }
                pattern_info = generate_SearchPatternD(sstable->filename, userKey, *key_range);
                break;
            }
            default:
                return Status::NotFound("Unknown packing type");
        }
        pattern_info.sstable_name = sstable->filename;
        search_package.searchPatterns.emplace_back(pattern_info);
    }
    if (search_package.searchPatterns.empty()) {
        return Status::NotFound("No valid SSTable patterns");
    }
    search_package.search_key = userKey.toString();
    search_package.header.pattern_num = static_cast<uint32_t>(search_package.searchPatterns.size());
    const std::string encoded_package = search_package.encode();

    if (encoded_package.empty()) {
        return Status::IOError("Encoded search package is empty");
    }
    search_package.dump();
    char* buffer  = (char*)allocateAligned(encoded_package.size());
    memcpy(buffer, encoded_package.data(), encoded_package.size());

    // TODO  
    // nvme_->nvme_write_search_package(buffer, encoded_package.size());

    free(buffer);
    
    return Status::OK();
}
#elif (SEARCH_PATTERN == 1)
Status API::search(std::string key ,std::string& value){
    if(key.empty()){
        return Status::IOError("Key string is empty");
    }
    std::cout << "Search key: " << key << std::endl;
    Key userKey(key);
    InternalKey internalKey(key);
    if (memtable_) {
        auto result = memtable_->Get(key);
        if (!result.has_value() && immutable_memtable_) {
            result = immutable_memtable_->Get(key);
        }
        if (result.has_value()) {
            Record rec = Record::Decode(*result);
            if(rec.internal_key.info.type == static_cast<uint8_t>(ValueType::kTypeDeletion)){
                return Status::NotFound("The key has been deleted");
            }
            value = rec.value;
            return Status::OK();
        }
    }

    auto sstables = lsmTree_->search_key(userKey);
    if (sstables.empty()){
        pr_info("No candidate SSTables found for key: %s", key.c_str());
        return Status::OK();
    }
    SearchPackageH search_package;


    while( !sstables.empty() ){
        auto sstable = sstables.front();
        std::cout   << "Find SStable: " << sstable->filename << "  Key range [ " << sstable->rangeMin.toString() << " ~ "
                    << sstable->rangeMax.toString() << " ]" <<std::endl;
        sstables.pop();
        SearchPatternH pattern_info;
        switch (packing_){
            case PackingType::kKeyPerPage:{
                std::string enc = userKey.encode();
                pattern_info.search_pattern = std::string(IMS_PAGE_SIZE, static_cast<char>(0xFF));
                memcpy(pattern_info.search_pattern.data(), enc.data(), enc.size());
                break;
            }
            case PackingType::kHash:{
                std::string enc = userKey.encode();
                pattern_info.search_pattern = std::string(IMS_PAGE_SIZE, static_cast<char>(0xFF));
                size_t slot_index = HashModN(internalKey, SLOT_NUM_PER_PAGE); 
                memcpy(pattern_info.search_pattern.data() + slot_index*SLOT_SIZE, enc.data(), enc.size());
                break;
            }
                
            case PackingType::kKeyRange:{
                auto key_range = keyRangeCache_->get(sstable->filename);
                if(key_range == std::nullopt){
                    key_range = read_key_range(sstable->filename);
                    if(!key_range.has_value()){
                        return Status::NotFound("Key not found in SSTable: " + sstable->filename);
                    }
                    keyRangeCache_->put(sstable->filename, *key_range);
                }
                pattern_info = generate_SearchPatternH(sstable->filename, userKey, *key_range);
                break;
            }
            default:
                return Status::NotFound("Unknown packing type");
        }
        pattern_info.sstable_name = sstable->filename;
        search_package.searchPatterns.emplace_back(pattern_info);
    }
    if (search_package.searchPatterns.empty()) {
        return Status::NotFound("No valid SSTable patterns");
    }
    search_package.search_key = userKey.toString();
    search_package.header.pattern_num = static_cast<uint32_t>(search_package.searchPatterns.size());
    const std::string encoded_package = search_package.encode();

    if (encoded_package.empty()) {
        return Status::IOError("Encoded search package is empty");
    }

    char* buffer  = (char*)allocateAligned(encoded_package.size());
    memcpy(buffer, encoded_package.data(), encoded_package.size());

    // TODO  
    // nvme_->nvme_write_search_package(buffer, encoded_package.size());

    free(buffer);
    
    return Status::OK();
}

#endif

Status API::range_query(std::string start_key,
                        std::string end_key,
                        std::set<std::string>& result_set) {
    if (start_key.empty() || end_key.empty()) {
        return Status::InvalidArgument("Start or end key string is empty");
    }
    if (start_key > end_key) {
        return Status::InvalidArgument("Start key cannot be greater than end key");
    }

    // 1) 準備 [lower, upper) 的 InternalKey（已編碼）
    const std::string lower =InternalKey(start_key, 0, ValueType::kTypeMin).Encode();
    const std::string upper =InternalKey(end_key, UINT64_MAX, ValueType::kTypeMax).Encode();

    // 2) 準備 MemTable iter（擁有權移交給 QueryIterator）
    std::unique_ptr<MemTableIterator> memIter;
    std::unique_ptr<MemTableIterator> immuteIter;

    if (memtable_) {
        // 假設 GetSkipList() 回傳的是 shared_ptr<SkipList<...>>
        // 且 MemTableIterator(list, const InternalKeyComparator*) 簽名成立
        memIter = std::make_unique<MemTableIterator>(memtable_->GetSkipList(), &icmp_);
    }
    if (immutable_memtable_) {
        immuteIter = std::make_unique<MemTableIterator>(immutable_memtable_->GetSkipList(), &icmp_);
    }
 
    QueryIterator it(getSSTable(),
                     getLogManager(),
                     getLSMTree(),
                     &icmp_,         // 若 icmp_ 本來就是指標，這裡改成 icmp_
                     std::move(memIter),
                     std::move(immuteIter));

    // 5) 設定區間 + Init
    it.SetInternalRange(lower, upper);
    Status s = it.Init();
    if (!s.ok()) return s;

    for (; it.Valid(); it.Next()) {
        std::string val;
        Status sv = it.ReadValue(val);
        if (!sv.ok()) {
            std::cout << sv.ToString() << std::endl;
            return sv;
        }
        InternalKey ik = InternalKey::Decode(std::string(it.key()));

        std::cout << "KEY: " << ik.UserKey() << "[seq: " << ik.info.seq << "] " << " -> VAL: " << val << "\n";
    }

    return Status::OK();
}



Status API::removeSSable(std::shared_ptr<TreeNode> rm){
    std::string filename = rm->filename;
    getSSTable()->eraseSSTable(filename);
    getLSMTree()->remove_sstable(rm);
    return Status::OK();
}



void API::compaction() {
    auto LowerSentinel = [](const std::string& uk) {
        return InternalKey(uk, UINT64_MAX, ValueType::kTypeMin);
    };
    auto UpperSentinel = [](const std::string& uk) {
        return InternalKey(uk,0,ValueType::kTypeMax);
    };

    bool compaction = false;
    // ---------- L0 -> L1 ----------
    if (getLSMTree()->get_level_num(0) >= LEVEL0_MAX) {
        pr_debug("Compaction start tree info:");
        lsmTree_->dump_lsmtere();
        compaction = true;
        pr_debug("Compaction triggered at Level 0");
        auto node = getLSMTree()->findLevel0Older();
        if (!node) return;

        pr_debug("Dump compaction source info:");
        node->dump();

        // 來源/目的候選：vector
        auto srcNodes = getLSMTree()->search_one_level(0, node->rangeMin, node->rangeMax);

        
        Key srcMin = node->rangeMin;
        Key srcMax = node->rangeMax;
        

        for (const auto& srcNode : srcNodes) {
            if(compareKey(srcNode->rangeMin,srcMin) < 0){
                srcMin = srcNode->rangeMin;
            }
            if(compareKey(srcNode->rangeMax,srcMax) > 0){
                srcMax = srcNode->rangeMax;
            }
        }
        InternalKey srcMinKey = UpperSentinel(srcMin.toString());
        InternalKey srcMaxKey = LowerSentinel(srcMax.toString());


        auto dstNodes = getLSMTree()->search_one_level(1, srcMin, srcMax);

        pr_debug("Dump source nodes info:");
        for(auto srcNode : srcNodes){
            
            srcNode->dump();   
        }
        pr_debug("Dump source nodes end");
        pr_debug("Dump destination nodes info:");
        for(auto dstNode : dstNodes){
            
            dstNode->dump();   
        }
        pr_debug("Dump destination nodes end");
        // 正確的 internal key 哨兵
        

        // 聚合目的層的 user key 範圍（允許為空）
        Key dstMinUser = node->rangeMin, dstMaxUser = node->rangeMax;
        bool hasDst = false;
        for (const auto& sp : dstNodes) {
            if (!sp) continue;
            if (!hasDst) {
                dstMinUser = sp->rangeMin;
                dstMaxUser = sp->rangeMax;
                hasDst = true;
            } else {
                if (compareKey(sp->rangeMin, dstMinUser) < 0) dstMinUser = sp->rangeMin;
                if (compareKey(sp->rangeMax, dstMaxUser) > 0) dstMaxUser = sp->rangeMax;
            }
        }

        InternalKey dstMinKey = LowerSentinel(dstMinUser.toString());
        InternalKey dstMaxKey = UpperSentinel(dstMaxUser.toString());

       

        CompactionRunner compaction(sstableManager_.get(), logManager_.get(),
                                    lsmTree_.get(), &icmp_, packing_,0,
                                    srcNodes,dstNodes);
        Status s = compaction.Run();
        if (s.ok()) {
            // 記錄下一輪起點（上界哨兵）
            set_compaction_key_list(srcMaxKey, 0);
            // 安全刪除（跳過 nullptr）
            for (const auto& sp : dstNodes) if (sp) removeSSable(sp);
            for (const auto& sp : srcNodes) if (sp) removeSSable(sp);
        } else {
            pr_debug("Compaction in level0 fail");
            return;
        }
    }

    // ---------- Lk -> Lk+1 ----------
    for (int level = 1; level < MAX_LEVEL; ++level) {
        if (!compactionTrigger(level)) continue;
        pr_debug("Compaction triggered at Level %d", level);
        pr_debug("Compaction start tree info:");
        lsmTree_->dump_lsmtere();
        compaction = true;
        
        auto &optKey = compaction_key_list_[level];
        if (!optKey.has_value()) {
            auto firstNode = getLSMTree()->getLevelFirstNode(level);
            if (!firstNode) continue;
            // 預設起點要用【下界哨兵】
            optKey = LowerSentinel(firstNode->rangeMin.toString());
        }

        auto srcNode = getLSMTree()->getNextNode(level, optKey->UserKey());
        std::vector<std::shared_ptr<TreeNode>> srcNodes;
        srcNodes.push_back(srcNode);
        if (!srcNode) {
            pr_debug("No next node at level %d", level);
            continue;
        }

        pr_debug("Dump compaction source info:");
        srcNode->dump();

        auto dstNodes = getLSMTree()->search_one_level(level + 1, srcNode->rangeMin, srcNode->rangeMax);

        InternalKey srcMinKey = LowerSentinel(srcNode->rangeMin.toString());
        InternalKey srcMaxKey = UpperSentinel(srcNode->rangeMax.toString());

        // 聚合目的層的 user key 範圍（允許為空）
        Key dstMinUser = srcNode->rangeMin, dstMaxUser = srcNode->rangeMax;
        bool hasDst = false;
        for (const auto& sp : dstNodes) {
            if (!sp) continue;
            if (!hasDst) {
                dstMinUser = sp->rangeMin;
                dstMaxUser = sp->rangeMax;
                hasDst = true;
            } else {
                if (compareKey(sp->rangeMin, dstMinUser) < 0) dstMinUser = sp->rangeMin;
                if (compareKey(sp->rangeMax, dstMaxUser) > 0) dstMaxUser = sp->rangeMax;
            }
        }

        InternalKey dstMinKey = LowerSentinel(dstMinUser.toString());
        InternalKey dstMaxKey = UpperSentinel(dstMaxUser.toString());

        CompactionPlan srcPlane(level,     srcMinKey.Encode(), srcMaxKey.Encode());
        CompactionPlan dstPlane(level + 1, dstMinKey.Encode(), dstMaxKey.Encode());

        CompactionRunner compaction(sstableManager_.get(), logManager_.get(),
                                    lsmTree_.get(), &icmp_, packing_,level,
                                    srcNodes, dstNodes);
        Status s = compaction.Run();
        if (s.ok()) {
            set_compaction_key_list(srcMaxKey, level);  // 更新進度（上界哨兵）
            for (const auto& sp : dstNodes) if (sp) removeSSable(sp);
            removeSSable(srcNode);
        }
    }
    if(compaction){
        pr_debug("Compaction result:");
        lsmTree_->dump_lsmtere();
    }
    
}



void API::test(){
    auto node = getLSMTree()->findLevel0Older();
    if (!node) return;

    pr_debug("Dump compaction source info:");
    node->dump();

    // 來源/目的候選：vector
    auto srcNodes = getLSMTree()->search_one_level(0, node->rangeMin, node->rangeMax);

    
    Key srcMin = node->rangeMin;
    Key srcMax = node->rangeMax;
    

    for (const auto& srcNode : srcNodes) {
        if(compareKey(srcMin,srcNode->rangeMin) < 0){
            srcMin = srcNode->rangeMin;
        }
        if(compareKey(srcMax,srcNode->rangeMax) < 0){
            srcMax = srcNode->rangeMax;
        }
    }

    Level0Iterator it(sstableManager_.get(),logManager_.get(),&icmp_,lsmTree_.get(),srcNodes,true);
    it.Init();
    it.SeekToFirst();
    while(it.Valid()){
        std::string value;
        it.ReadValue(value);
        InternalKey k = InternalKey::Decode(std::string(it.key()));
        std::cout << "Key: " << k.UserKey() << " -> Value: " << value << std::endl;
        it.Next();
    }
}


bool API::compactionTrigger(int level){
    if(level < 0 || level >= MAX_LEVEL) throw std::out_of_range("Level out of range");
    switch (level){
        case 0: return getLSMTree()->get_level_num(0) >= LEVEL0_MAX;
        case 1: return getLSMTree()->get_level_num(1) >= LEVEL1_MAX;
        case 2: return getLSMTree()->get_level_num(2) >= LEVEL2_MAX;
        case 3: return getLSMTree()->get_level_num(3) >= LEVEL3_MAX;
        case 4: return getLSMTree()->get_level_num(4) >= LEVEL4_MAX;
        case 5: return getLSMTree()->get_level_num(5) >= LEVEL5_MAX;
        case 6: return getLSMTree()->get_level_num(6) >= LEVEL6_MAX;
        default: return false;
    }
}


void API::init_compaction_key_list(){
    compaction_key_list_.clear();
    for(int i = 0 ; i < MAX_LEVEL ; i++){
        auto node = getLSMTree()->getLevelFirstNode(i);
        if(node == nullptr){
            compaction_key_list_.push_back(std::nullopt);
            continue;
        }
        InternalKey min(node->rangeMin.toString(),0,ValueType::kTypeMin);
        compaction_key_list_.push_back(min);
    }
}
void API::set_compaction_key_list(InternalKey key , int level){
    if(level < 0 || level >= MAX_LEVEL){
        throw std::out_of_range("Level out of range");
    }
    compaction_key_list_[level] = key;
}


void API::OnSSTableFlushed(const sstable_info& info) {
    std::lock_guard<std::mutex> lk(mu_);

    if (immutable_memtable_) {
        immutable_memtable_.reset();
        std::cout << "[API] Immutable memtable cleared after flush to " << info.filename << "\n";
    }
}

void API::OnSSTableWriteFailed(const sstable_info& info, int err) {
    std::lock_guard<std::mutex> lk(mu_);
    std::cerr << "[API] Flush failed for " << info.filename << ", err=" << err
              << " (keeping immutable memtable for retry)\n";
}


void API::log_garbage_collection(){
    int gcBlockNum = GC_BLOCK_NUM;
    while(gcBlockNum > 0){
        uint32_t valid_offset = logManager_->get_first_block_offset();
        uint32_t lbn = logManager_->get_log_list_front();
        if(lbn >= LBN_NUM){
            pr_debug("Invalid LBN for GC: %u", lbn);
            break;
        }
        if(valid_offset >= BLOCK_SIZE){
            pr_debug("Invalid offset for GC: %u", valid_offset);
            break;
        }
        uint32_t next_block_valid_offset = 0;
        auto records = logManager_->readLogBlock(lbn,valid_offset,next_block_valid_offset);

        if (next_block_valid_offset == UINT32_MAX) {
            pr_debug("GC cross-page read failed for LBN %u; abort this GC cycle to avoid data loss", lbn);
            break;
        }
        if (next_block_valid_offset >= BLOCK_SIZE) {
            pr_debug("GC got invalid next_block_valid_offset=%u for LBN %u; abort", next_block_valid_offset, lbn);
            break;
        }
        if (records.empty() && next_block_valid_offset == 0) {
            pr_debug("GC readLogBlock returned empty for LBN %u (offset=%u); stop to avoid data loss",
                     lbn, valid_offset);
            break;
        }
        

        Record rec;
        Status s;
        for(const auto record : records){
            if (static_cast<uint8_t>(record.internal_key.info.type) == static_cast<uint8_t>(ValueType::kTypeDeletion)) {
                pr_debug("GC skip tombstone key: %s", record.internal_key.UserKey().c_str());
                continue;
            }
            s = get(record.internal_key.UserKey(),rec);
            if(!s.ok()){
                if(s.IsNotFound()){
                    pr_debug("GC key: %s is deleted", record.internal_key.UserKey().c_str());
                    continue;
                } else {
                    pr_debug("Failed to get key: %s during GC", record.internal_key.UserKey().c_str());
                    continue;
                }
            }
            // std::cout << "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" << std::endl;
            // record.Dump();
            // std::cout << "===========================================================" << std::endl;
            // rec.Dump();
            // std::cout << "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" << std::endl;
            // if(rec.internal_key.value_ptr.lpn != record.internal_key.value_ptr.lpn){
            //     pr_debug("log lpn is mismatch,LPN(%lu) in log , but LPN(%lu) in LSM tree"
            //             ,record.internal_key.value_ptr.lpn,rec.internal_key.value_ptr.lpn);
            // }
            // if(rec.internal_key.value_ptr.offset != record.internal_key.value_ptr.offset){
            //     pr_debug("log offset is mismatch,offset(%lu) in log , but offset(%lu) in LSM tree"
            //             ,record.internal_key.value_ptr.offset,rec.internal_key.value_ptr.offset);
            // }
            // if(rec.value_size != record.value_size){
            //     pr_debug("log value size is mismatch,offset(%lu) in log , but offset(%lu) in LSM tree"
            //             ,record.value_size,rec.value_size);
            // }
            bool still_live =
                rec.internal_key.value_ptr.lpn    == record.internal_key.value_ptr.lpn &&
                rec.internal_key.value_ptr.offset == record.internal_key.value_ptr.offset &&
                rec.value_size                    == record.value_size;

            if (still_live) {
                pr_info("GC rewrite live key: %s", record.internal_key.UserKey().c_str());
                Status ps = put_from_gc(record.internal_key.UserKey(), record.value); // 或 std::move(record.value)
                if (!ps.ok()) {
                    pr_debug("GC put() failed for key: %s: %s",
                            record.internal_key.UserKey().c_str(), ps.ToString().c_str());
                }
            }
            else{
                pr_debug("GC key: %s has newer version, skip", record.internal_key.UserKey().c_str());
            }
        }
        

        logManager_->remove_log_front();
        logManager_->set_first_block_offset_(next_block_valid_offset);
        --gcBlockNum;
    }
}