#include "memtable.hh"

#include <iostream>
#include <string_view>
#include <functional>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

MemTable::MemTable()
    : hash_num_(IMS_PAGE_SIZE / sizeof(InternalKey), 0) {}

void MemTable::Put(const Record &rec) {
    skiplist_.Insert(rec);
    size_ += sizeof(InternalKey);
    ++node_count_;
    hash_num_[HashModN(rec.internal_key, hash_num_.size())]++;
}

std::optional<std::string> MemTable::Get(const std::string& user_key) const {
    SkipList<Record, RecordComparator>::Iterator iter = skiplist_.GetIterator();
    InternalKey lookup(user_key, UINT64_MAX, ValueType::kTypeValue); 
    Record lookup_rec(lookup, "");

    iter.Seek(lookup_rec);
    if (iter.Valid() && iter.record().internal_key.UserKey() == user_key) {
        const auto& record = iter.record();
        if (static_cast<ValueType>(record.internal_key.info.type) == ValueType::kTypeDeletion) {
            return std::nullopt;
        }
        return record.value;
    }
    return std::nullopt;
}


void MemTable::Dump() const {
    auto iter = skiplist_.GetIterator();
    iter.SeekToFirst();
    std::cout << "== MemTable Dump ==" << std::endl;
    std::cout << "Total Nodes: " << skiplist_.get_node_num() << std::endl;
    while (iter.Valid()) {
        const auto& record = iter.record();
        const auto& key = record.internal_key;
        std::cout << key.UserKey()
                  << " [seq=" << key.info.seq
                  << ", type=" << ((static_cast<int>(key.info.type) == 0) ? "Delete" : "Insert")
                  << ", lpn=" << key.value_ptr.lpn
                  << ", offset=" << key.value_ptr.offset
                  << "] => " << record.value
                  << "\n";
        iter.Next();
    }
    std::cout << "=======================" << std::endl;
}

size_t MemTable::ApproximateMemoryUsage() const {
    return size_;
}

bool MemTable::memTableIsFull() {
    switch (packing_type_) {
        case static_cast<int>(PackingType::kKeyPerPage):
            return node_count_ >= IMS_PAGE_NUM;
        case static_cast<int>(PackingType::kHash):
            return std::any_of(hash_num_.begin(), hash_num_.end(),
                               [](uint32_t count) { return count >= IMS_PAGE_NUM; });
        case static_cast<int>(PackingType::kKeyRange):
            return size_ >= IMS_PAGE_SIZE;
        default:
            return false;
    }
}

static void* AllocateAligned(size_t size) {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, 4096, size) != 0 || ptr == nullptr) {
        throw std::bad_alloc();
    }
    // 先歸零
    std::memset(ptr, 0, size);
    return ptr;
}



size_t HashModN(const InternalKey& ikey, size_t n) {
    std::string_view data = ikey.Encode(); 

    std::hash<std::string_view> hasher;
    size_t hash_value = hasher(data);
    return hash_value % n;
}



MemTableIterator::MemTableIterator(
    const std::shared_ptr<SkipList<Record, RecordComparator>>& list,
    const InternalKeyComparator* icmp)
: list_(list)
, it_(list_->GetIterator()) 
, icmp_(icmp) {
    
    key_buf_.clear();
    value_buf_ = {};
}

bool MemTableIterator::Valid() const {
    return it_.Valid();
}

void MemTableIterator::SeekToFirst() {
    it_.SeekToFirst();
    SyncKV();
}

void MemTableIterator::SeekToLast() {
    it_.SeekToLast();
    SyncKV();
}

void MemTableIterator::Seek(std::string_view internal_target) {
    InternalKey inkey   = InternalKey(internal_target.data());
    Record      rec     = Record(inkey);
    it_.Seek(rec);
    SyncKV();
}

void MemTableIterator::Next() {
    it_.Next();
    SyncKV();
}

void MemTableIterator::Prev() {
    it_.Prev();
    SyncKV();
}

void MemTableIterator::SyncKV() {
    if (!it_.Valid()) {
        key_buf_.clear();
        value_buf_ = {};
        return;
    }
    const Record& r = it_.record();

    key_buf_ = r.internal_key.UserKey();
    value_buf_ = r.value;
}