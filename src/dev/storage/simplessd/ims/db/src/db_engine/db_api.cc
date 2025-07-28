#include "db_api.hh"
#include "nvme_interface.hh"
#include "IMS_interface.hh"
API::API(){
    nvme_ = std::make_unique<NVMe>();
    packing_ = PACKING_T;
    memtable_ = std::make_unique<MemTable>();
    immutable_memtable_ = nullptr;
    logManager_ = std::make_unique<LOG_MANAGER>(*nvme_);
    global_seq_ = 0;
    sstableManager_ = std::make_unique<SstableManager>(*nvme_);
}


Status API::put(std::string key ,std::string value){
    if (!memtable_) {
        memtable_ = std::make_unique<MemTable>();
    }
    if (memtable_->memTableIsFull()) {
        immutable_memtable_ = std::move(memtable_);
        memtable_ = std::make_unique<MemTable>();
        std::string buffer( sstableManager_->packingTable(immutable_memtable_->GetSkipList()) );
        assert(buffer.size() != BLOCK_SIZE);
        if (buffer.empty()) {
            std::cerr << "[ERROR] packingTable returned empty buffer!" << std::endl;
            return Status::IOError("Packing failed");
        }
        sstableManager_->writeSSTable(0, immutable_memtable_->getMinKey(), immutable_memtable_->getMaxKey(), buffer);

    }
    uint32_t lpn = 0;
    uint32_t offset = 0;
    logManager_->getLPN(lpn,offset);
    uint64_t seq = global_seq_.fetch_add(1); 
    InternalKey internal_key(key,lpn,offset,seq,ValueType::kTypeValue);
    Record internal_value(internal_key,value);
    memtable_->Put(internal_value);
    return Status::OK();
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
        sstableManager_->dump_lsmtere();
    } else {
        std::cout << "SSTable Manager is not initialized." << std::endl;
    }
};