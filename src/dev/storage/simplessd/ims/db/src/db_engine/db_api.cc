#include "db_api.hh"

API::API(){
    packing_ = PACKING_T;

}


Status API::put(std::string key ,std::string value){
    if (!memtable_) {
        memtable_ = std::make_unique<MemTable>();
    }
    if (memtable_->memTableIsFull()) {
        immutable_memtable_ = std::move(memtable_);
        memtable_ = std::make_unique<MemTable>();
        std::string buffer( sstableManager_.packingTable(immutable_memtable_->GetSkipList()) );
        if (buffer.empty()) {
            std::cerr << "[ERROR] packingTable returned empty buffer!" << std::endl;
            return Status::IOError("Packing failed");
        }
        sstableManager_.writeSSTable(0, immutable_memtable_->getMinKey(), immutable_memtable_->getMaxKey(), buffer);
    }
    uint32_t lpn = 0;
    uint32_t offset = 0;
    logManager_.getLPN(lpn,offset);
    uint64_t seq = global_seq_.fetch_add(1); 
    InternalKey internal_key(key,lpn,offset,seq,ValueType::kTypeValue);
    Record internal_value(internal_key,value);
    memtable_->Put(internal_value);
    return Status::OK();
}