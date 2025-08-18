#include "db_api.hh"
#include "nvme_interface.hh"
#include "nvme_test.hh"
#include "IMS_interface.hh"
#include "lsmtree.hh"
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
    logManager_ = std::make_unique<LOG_MANAGER>(*nvme_);
    global_seq_ = 0;
    sstableManager_ = std::make_unique<SstableManager>(*nvme_,*lsmTree_);
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

    auto opt = DB_INIT::decode(buf);        
    if (!opt) {
        return Status::Corruption("DB_INIT decode failed");
    }
    DB_INIT info = *opt;                          
    getLogManager()->setNextLBN(info.next_lbn);
    getLogManager()->setCurrentLBN(info.current_lbn);
    getLogManager()->setPageOffset(info.page_offset);
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


Status API::put(std::string key ,std::string value){
    if (!memtable_) {
        memtable_ = std::make_unique<MemTable>();
    }
    if (memtable_->memTableIsFull()) {
        immutable_memtable_ = std::move(memtable_);
        memtable_ = std::make_unique<MemTable>();
        
        std::string buffer( sstableManager_->packingTable(immutable_memtable_->GetSkipList()) );
        assert(buffer.size() == BLOCK_SIZE);
        if (buffer.empty()) {
            std::cerr << "[ERROR] packingTable returned empty buffer!" << std::endl;
            return Status::IOError("Packing failed");
        }
        sstableManager_->writeSSTable(0, immutable_memtable_->getMinKey(), immutable_memtable_->getMaxKey(), buffer);

    }
    uint32_t lpn = 0;
    uint32_t offset = 0;
    getLogManager()->getLPN(lpn, offset);
    uint64_t seq = global_seq_.fetch_add(1); 
    InternalKey internal_key(key,lpn,offset,seq,ValueType::kTypeValue);
    Record internal_value(internal_key,value);
    logManager_->writeLog(internal_value);
    memtable_->Put(internal_value);
    return Status::OK();
}


Status API::delete_key(std::string key ,std::string value){
    if (!memtable_) {
        memtable_ = std::make_unique<MemTable>();
    }
    if (memtable_->memTableIsFull()) {
        immutable_memtable_ = std::move(memtable_);
        memtable_ = std::make_unique<MemTable>();
        
        std::string buffer( sstableManager_->packingTable(immutable_memtable_->GetSkipList()) );
        assert(buffer.size() == BLOCK_SIZE);
        if (buffer.empty()) {
            std::cerr << "[ERROR] packingTable returned empty buffer!" << std::endl;
            return Status::IOError("Packing failed");
        }
        sstableManager_->writeSSTable(0, immutable_memtable_->getMinKey(), immutable_memtable_->getMaxKey(), buffer);

    }
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
    std::cout << "Search key: " << key << std::endl;
    Key interkey(key);
    InternalKey search_key(key);
    auto result = memtable_->Get(key);
    if(!result.has_value() && immutable_memtable_){
        result = immutable_memtable_->Get(key);
    }
    int level = 0;
    if(!result.has_value()){
        auto sstables = lsmTree_->search_key(interkey);
        char * buffer = (char *)allocateAligned(BLOCK_SIZE);
        while(!result.has_value() && !sstables.empty()){
            auto sstable = sstables.front();
            std::cout   << "Find SStable: " << sstable->filename << "  Key range [ " << sstable->rangeMin.toString() << " ~ "
                        << sstable->rangeMax.toString() << " ]" <<std::endl;
            sstables.pop();
            sstableManager_->readSSTable(sstable->filename,buffer);
            getSSTable()->waitAllTasksDone();
            auto keys = parse_sstable(buffer);
            auto it = keys.find(search_key);
            if(it != keys.end()){
                it->dump();
                uint32_t lpn = it->value_ptr.lpn;
                uint32_t offset = it->value_ptr.offset;
                if(it->info.type == static_cast<uint8_t>(ValueType::kTypeDeletion) ){
                    std::cout << "Key is not found ,becasue this key has been deleted" << std::endl;
                    free(buffer);
                    return Status::NotFound("The key has been deleted");
                }
                auto record = logManager_->readLog(lpn, offset);
                record->Dump();
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


std::set<InternalKey ,SetComparator> API::parse_sstable(char* buffer) {
    size_t offset = 0;
    std::set<InternalKey ,SetComparator> keys;

    while (offset + sizeof(InternalKey) <= BLOCK_SIZE) {
        InternalKey key;
        key = InternalKey::Decode( (buffer + offset));
        offset += sizeof(InternalKey);
        if(key.info.type == INVALID_KEY_TYPE){
            continue;
        }
        keys.insert(key);
    }

    return keys;
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
    int err = nvme_->nvme_read_sstable(filename, buffer);
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


SearchPattern API::generate_search_slot(const std::string& filename, const Key& key,const std::set<std::string>& keys){
    if(filename.empty()){
        throw std::invalid_argument("Filename cannot be empty");
    }
    if (filename.size() != 36) {
        throw std::invalid_argument("sstable_name must be 36 bytes");
    }
    if(keys.empty()){
        throw std::invalid_argument("Keys set cannot be empty");
    }

    auto it = keys.upper_bound(key.toString());
    if(it == keys.begin()){
        throw std::out_of_range("No predecessor: provided key is smaller than the smallest key");
    }
    SearchPattern pattern;
    pattern.slot_index = static_cast<uint32_t>(std::distance(keys.begin(), std::prev(it)));
    pattern.sstable_name = filename;
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

Status API::search(std::string key ,std::string& value){
    if(key.empty()){
        return Status::IOError("Key string is empty");
    }
    std::cout << "Search key: " << key << std::endl;
    Key userKey(key);
    InternalKey internalKey(key);
    auto result = memtable_->Get(key);
    if(!result.has_value() && immutable_memtable_){
        result = immutable_memtable_->Get(key);
    }
    if(result.has_value()){
        value = result.value();
        return Status::OK();
    }
    auto sstables = lsmTree_->search_key(userKey);
    if (sstables.empty()) return Status::NotFound("No candidate SSTables");
    SearchPackage search_package;


    while( !sstables.empty() ){
        auto sstable = sstables.front();
        std::cout   << "Find SStable: " << sstable->filename << "  Key range [ " << sstable->rangeMin.toString() << " ~ "
                    << sstable->rangeMax.toString() << " ]" <<std::endl;
        sstables.pop();
        SearchPattern pattern_info;
        switch (packing_){
        case PackingType::kKeyPerPage:{
            pattern_info.slot_index = 0;
            break;
        }
        case PackingType::kHash:{
            pattern_info.slot_index = HashModN(internalKey, SLOT_NUM); 
            break;
        }
            
        case PackingType::kKeyRange:{
            auto key_range = read_cache_->get(sstable->filename);
            if(key_range == std::nullopt){
                key_range = read_key_range(sstable->filename);
                if(key_range == std::nullopt){
                    return Status::NotFound("Key not found in SSTable: " + sstable->filename);
                }
                read_cache_->put(sstable->filename, *key_range);
            }
            pattern_info = generate_search_slot(sstable->filename, userKey, *key_range);
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

    search_package.header.pattern_num = static_cast<uint32_t>(search_package.searchPatterns.size());
    auto encoded_package = search_package.encode();
    char* buffer  = (char*)allocateAligned(BLOCK_SIZE);
    memcpy(buffer, encoded_package.data(), encoded_package.size());
    // nvme_->nvme_write_search_package(buffer, encoded_package.size());

    
    return Status::OK();
}

Status API::range_query(std::string start_key, std::string end_key, std::set<std::string>& result_set){
    if(start_key.empty() || end_key.empty()){
        return Status::IOError("Start or end key string is empty");
    }
    if(start_key > end_key){
        return Status::IOError("Start key cannot be greater than end key");
    }
    
    Key start(start_key);
    Key end(end_key);
    auto sstables = lsmTree_->search_all_level(start, end);
    if (sstables.empty()) return Status::NotFound("No candidate SSTables for range query");

    // for (const auto& sstable : sstables) {
    //     std::cout   << "Find SStable: " << sstable << "  Key range [ " << sstable->rangeMin.toString() << " ~ "
    //                 << sstable->rangeMax.toString() << " ]" <<std::endl;
    //     char* buffer = (char*)allocateAligned(BLOCK_SIZE);
    //     nvme_->nvme_read_sstable(sstable->filename, buffer);
    //     getSSTable()->waitAllTasksDone();
    //     auto keys = parse_sstable_page(buffer);
    //     for (const auto& key : keys) {
    //         if (key.UserKey() >= start_key && key.UserKey() <= end_key) {
    //             result_set.insert(key.UserKey());
    //         }
    //     }
    //     free(buffer);
    // }
    
    return Status::OK();
}