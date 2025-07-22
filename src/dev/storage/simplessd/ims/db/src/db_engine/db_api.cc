#include "db_api.hh"

Status API::put(std::string key ,std::string value){
    if (!memtable_) {
        memtable_ = std::make_unique<MemTable>();
    }
    if (memtable_->memTableIsFull()) {
        immutable_memtable_ = std::move(memtable_);
        memtable_ = std::make_unique<MemTable>();

        // 可通知背景 thread 做 flush（例如加條件變數）
        // NotifyFlush();
    }
    uint32_t lpn = 0;
    uint32_t offset = 0;
    logManager_.getLPN(lpn,offset);
    uint64_t seq = global_seq_.fetch_add(1); 
    InternalKey internal_key(key,lpn,offset,seq,ValueType::kTypeValue);
    Record internal_value(internal_key,value);
    memtable_->Put(internal_value);
    memtable_->Dump();
    return Status::OK();
}