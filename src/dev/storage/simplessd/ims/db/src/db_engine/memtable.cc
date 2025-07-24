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
    InternalKey lookup(user_key, UINT64_MAX, ValueType::kTypeValue);  // 最大 seq，保證找最新
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
    while (iter.Valid()) {
        const auto& record = iter.record();
        const auto& key = record.internal_key;
        std::cout << key.UserKey()
                  << " [seq=" << key.info.seq
                  << ", type=" << static_cast<int>(key.info.type)
                  << ", lpn=" << key.value_ptr.lpn
                  << ", offset=" << key.value_ptr.offset
                  << "] => " << record.value << "\n";
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



std::string MemTable::keyPerPagePacking() {
    std::string package;
    auto it = skiplist_.GetIterator();
    it.SeekToFirst();

    while (it.Valid()) {
        InternalKey key = it.record().internal_key;
        std::string encode = key.Encode();

        if (encode.size() > IMS_PAGE_SIZE) {
            throw std::runtime_error("Encoded key size exceeds IMS_PAGE_SIZE");
        }

        encode.append(IMS_PAGE_SIZE - encode.size(), 0);
        package.append(encode);

        it.Next();
    }
    if (package.size() != IMS_PAGE_NUM * IMS_PAGE_SIZE) {
        throw std::runtime_error("Packed data size does not match expected size");
    }
    return package;
}

std::string MemTable::keyHashPacking(){
    const size_t slots_per_page = IMS_PAGE_SIZE / sizeof(InternalKey);
    const size_t total_slots = IMS_PAGE_NUM * slots_per_page;
    std::string block(total_slots * sizeof(InternalKey), 0);

    auto it = skiplist_.GetIterator();
    it.SeekToFirst();

    while (it.Valid()) {
        const InternalKey& key = it.record().internal_key;

        size_t slot_idx = HashModN(key, slots_per_page);

        bool placed = false;
        for (size_t page = 0; page < IMS_PAGE_NUM; ++page) {
            size_t idx = page * slots_per_page + slot_idx;
            size_t offset = idx * sizeof(InternalKey);

            InternalKey* ptr = reinterpret_cast<InternalKey*>(&block[offset]);
            if (ptr->key_size == 0) {  
                *ptr = key;
                placed = true;
                break;
            }
        }

        if (!placed) {
            throw std::runtime_error("Block full, cannot place key.");
        }

        it.Next();
    }

    if (block.size() != IMS_PAGE_NUM * IMS_PAGE_SIZE) {
        throw std::runtime_error("Packed data size does not match expected size");
    }
    return block;
}


std::string MemTable::keyRangePacking() {
    const size_t slots_per_page = IMS_PAGE_SIZE / sizeof(InternalKey);
    const size_t total_slots = IMS_PAGE_NUM * slots_per_page;

    std::string block(total_slots * sizeof(InternalKey), 0);

    auto iter = skiplist_.GetIterator();
    iter.SeekToFirst();

    size_t slot_index = 0;

    for (size_t slot = 0; slot < slots_per_page; ++slot) {
        for (size_t page = 0; page < IMS_PAGE_NUM; ++page) {
            if (!iter.Valid()) break;

            size_t flat_index = page * slots_per_page + slot;
            size_t offset = flat_index * sizeof(InternalKey);

            InternalKey* ptr = reinterpret_cast<InternalKey*>(&block[offset]);
            *ptr = iter.record().internal_key;

            iter.Next();
        }
    }
    if (block.size() != IMS_PAGE_NUM * IMS_PAGE_SIZE) {
        throw std::runtime_error("Packed data size does not match expected size");
    }
    return block;
}


void MemTable::packingTable(){
    std::string package;
    switch (packing_type_)
    {
        case 0:
            package = keyPerPagePacking();
            break;
        case 1:
            package = keyHashPacking();
            break;
        case 2:
            package = keyRangePacking();
            break;
        default:
            break;
    }
}

size_t HashModN(const InternalKey& ikey, size_t n) {
    if (n == 0) return 0;
    std::string_view view(reinterpret_cast<const char*>(ikey.key), ikey.key_size);
    size_t hash_val = std::hash<std::string_view>{}(view);
    return hash_val % n;
}
