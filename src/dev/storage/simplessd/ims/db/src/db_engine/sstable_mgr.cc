#include "sstable_mgr.hh"
#include "def.hh"
#include "internal_key.hh"
#include "nvme_interface.hh"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include "internal_key.hh"

std::string SstableManager::packingTable(const SkipList<Record, RecordComparator>& skiplist){
    std::string package;
    switch (packing_type_)
    {
        case 0:
            package = std::string(keyPerPagePacking(skiplist), BLOCK_SIZE);
            break;
        case 1:
            package = std::string(keyHashPacking(skiplist),BLOCK_SIZE);
            break;
        case 2:
            package = std::string(keyRangePacking(skiplist),BLOCK_SIZE);
            break;
        default:
            break;
    }
    return package;
}


char* SstableManager::keyPerPagePacking(const SkipList<Record, RecordComparator>& skiplist) {
    const size_t total_size = IMS_PAGE_NUM * IMS_PAGE_SIZE;
    char* buffer = static_cast<char*>(allocateAligned(total_size));
    if (!buffer) throw std::runtime_error("Failed to allocate aligned buffer");

    std::memset(buffer, 0xFF, total_size);  // optional: fill with default value

    auto it = skiplist.GetIterator();
    it.SeekToFirst();

    size_t page = 0;
    while (it.Valid()) {
        if (page > IMS_PAGE_NUM) {
            free(buffer);
            throw std::runtime_error("Too many records for fixed page count (IMS_PAGE_NUM)");
        }

        InternalKey key = it.record().internal_key;
        std::string enc = key.Encode();

        if (enc.size() > IMS_PAGE_SIZE) {
            free(buffer);
            throw std::runtime_error("Encoded key size exceeds IMS_PAGE_SIZE");
        }

        char* dst = buffer + page * IMS_PAGE_SIZE;
        std::memcpy(dst, enc.data(), enc.size());

        ++page;
        it.Next();
    }

    return buffer;
}



char* SstableManager::keyHashPacking(const SkipList<Record, RecordComparator>& skiplist) {
    const size_t slots_per_page = IMS_PAGE_SIZE / sizeof(InternalKey);
    const size_t total_slots = IMS_PAGE_NUM * slots_per_page;
    const size_t block_size = total_slots * sizeof(InternalKey);

    char* buffer = static_cast<char*>(allocateAligned(block_size));
    auto it = skiplist.GetIterator();
    it.SeekToFirst();

    while (it.Valid()) {
        const InternalKey& key = it.record().internal_key;
        size_t slot_idx = HashModN(key, slots_per_page);
        bool placed = false;

        for (size_t pg = 0; pg < IMS_PAGE_NUM; ++pg) {
            size_t idx = pg * slots_per_page + slot_idx;
            size_t offset = idx * sizeof(InternalKey);
            InternalKey* ptr = reinterpret_cast<InternalKey*>(buffer + offset);
            if (ptr->key.key_size == 0) {
                *ptr = key;
                placed = true;
                break;
            }
        }

        if (!placed) {
            free(buffer);
            throw std::runtime_error("Hash block full, cannot place key");
        }

        it.Next();
    }

    return buffer;
}

char* SstableManager::keyRangePacking(const SkipList<Record, RecordComparator>& skiplist) {
    const size_t slots_per_page = IMS_PAGE_SIZE / sizeof(InternalKey);
    const size_t total_slots = IMS_PAGE_NUM * slots_per_page;
    const size_t block_size = total_slots * sizeof(InternalKey);

    char* buffer = static_cast<char*>(allocateAligned(block_size));
    if (!buffer) {
        throw std::bad_alloc();
    }

    auto iter = skiplist.GetIterator();
    iter.SeekToFirst();

    for (size_t slot = 0; slot < slots_per_page; ++slot) {
        for (size_t page = 0; page < IMS_PAGE_NUM; ++page) {
            if (!iter.Valid()) break;

            size_t flat_index = page * slots_per_page + slot;
            size_t offset = flat_index * sizeof(InternalKey);

            InternalKey* ptr = reinterpret_cast<InternalKey*>(buffer + offset);
            *ptr = iter.record().internal_key;

            iter.Next();
        }
    }

    return buffer;
}

std::string SstableManager::generateFilename(uint32_t seq) {
    constexpr size_t max_digits = sizeof(mappingEntry{}.fileName) - 1;
    std::ostringstream oss;
    oss << std::setw(max_digits) << std::setfill('0') << seq;
    auto str = oss.str();
    assert(str.size() <= max_digits);
    return str;
}

void SstableManager::init() {}

void SstableManager::readSSTable(const std::string& filename,char *buffer) {
    
    if (!buffer) {
        std::cerr << "Failed to allocate buffer for reading SSTable\n";
        return;
    }

    thread_pool_.Submit([filename, buffer, this]() {
        std::cout << "Reading SSTable from: " << filename << std::endl;

        int err = nvme_.nvme_read_sstable(filename, buffer);
        if (err == COMMAND_FAILED) {
            std::cerr << "[Thread] Failed to read SSTable: " << filename << std::endl;
            std::free(buffer);
            return;
        }
        std::cout << "[Thread] Read success: " << filename << std::endl;
    });

    std::cout << "[Main] Async read dispatched.\n";
}


void SstableManager::writeSSTable(uint8_t level, InternalKey minKey, InternalKey maxKey, std::string sstable_buffer) {
    if (sstable_buffer.empty()) {
        std::cerr << "SSTable buffer cannot be null" << std::endl;
        return;
    }

    Key rangeMinKey = minKey.key;
    Key rangeMaxKey = maxKey.key;
    std::string filename = generateFilename(sequenceNumber_.fetch_add(1));

    sstable_info info(filename, level, rangeMinKey, rangeMaxKey);
    std::cout << "Dispatching write for SSTable: " << filename << std::endl;
    info.dump();

    thread_pool_.Submit([info, buf = std::move(sstable_buffer), this]() {
        std::cout << "[Thread] Entered thread task\n";
        int err = nvme_.nvme_write_sstable(info,  const_cast<char*>(buf.data()));
        std::cout << "[Thread] nvme_write_sstable returned " << err << std::endl;
        if (err == COMMAND_FAILED) {
            std::cerr << "[Thread] Failed to write SSTable: " << info.filename << std::endl;
            return;
        }
        std::cout << "[Thread] Write success: " << info.filename << std::endl;

        auto node = std::make_shared<TreeNode>(info.filename,
                                            info.level,
                                            info.min, 
                                            info.max);
        {
            std::unique_lock<std::mutex> lock(tree_mutex_);
            lsmTree_.insert_sstable(node);
        }
        

        std::cout << "SStable(" << info.filename << ") written successfully.\n";
    });

    std::cout << "[Main] Async write dispatched.\n";
}

// TODO
void SstableManager::deleteSSTable(const std::string& filename) {

}


Status SstableIterator::Init(){
    entries_.clear();
    pos_ = 0;
    sstable_mgr_->readSSTable(filename_,buf_);

    st_ = Status::OK();
    return st_; 
}

bool SstableIterator::Valid() const {
    if (!st_.ok()) return false;
    return (pos_ >= 0) && (pos_ < static_cast<int>(entries_.size()));
}

void SstableIterator::SeekToFirst() {
    if (entries_.empty()) { pos_ = -1; return; }
    pos_ = 0;
}

void SstableIterator::SeekToLast()  {
    if (entries_.empty()) { pos_ = -1; return; }
    pos_ = static_cast<int>(entries_.size()) - 1;
}
void SstableIterator::Seek(std::string_view internal_target) {
    if (entries_.empty()) { pos_ = -1; return; }

    // 预解码 target（仅在使用 icmp_ 时需要）
    InternalKey target;
    if (icmp_) {
        target = InternalKey::Decode(internal_target.data());
        // 如果 Decode 有失败分支，这里要设置 st_ 并 return
    }

    auto is_less = [&](std::string_view a_sv) -> bool {
        if (icmp_) {
            InternalKey ia = InternalKey::Decode(a_sv.data());
            return (*icmp_)(ia, target);  // a < target ?
        } else {
            int c = std::memcmp(a_sv.data(), internal_target.data(),
                                std::min(a_sv.size(), internal_target.size()));
            if (c != 0) return c < 0;
            return a_sv.size() < internal_target.size();
        }
    };

    int l = 0, r = static_cast<int>(entries_.size()) - 1;
    int ans = static_cast<int>(entries_.size());
    while (l <= r) {
        int m = (l + r) >> 1;
        if (is_less(entry_key(entries_[m]))) {
            l = m + 1;
        } else {
            ans = m;
            r = m - 1;
        }
    }
    pos_ = (ans == static_cast<int>(entries_.size())) ? -1 : ans;
}



void SstableIterator::Next() {
    if (!Valid()) return;
    ++pos_;
    if (pos_ >= static_cast<int>(entries_.size())) pos_ = -1;
}

void SstableIterator::Prev() {
    if (!Valid()) return;
    --pos_;
    if (pos_ < 0) pos_ = -1;
}

std::string_view SstableIterator::key() const {
    if (!Valid()) return {};
    return entry_key(entries_[pos_]);
}


std::string SstableIterator::value() const {
    const auto& e = entries_[pos_];
    InternalKey *ink_ptr = reinterpret_cast<InternalKey*>(buf_+e.key_off);
    auto record = log_mgr_->readLog(ink_ptr->value_ptr.lpn,ink_ptr->value_ptr.offset);
    if(record.has_value()){
        return std::string_view(record.value());
    }
    return std::string_view(buf_ + e.val_off, e.val_len);
}

Status SstableIterator::status() const {
    return st_;
}


std::vector<SstableIterator::EntryRef> SstableIterator::gen_sorted_view(){
    std::vector<EntryRef> sorted_vec;
    size_t slot_offset = 0;
    if(!buf_){
        return sorted_vec;
    }
    switch(static_cast<int>(type_)){
        case static_cast<int>(PackingType::kKeyPerPage):
            while( slot_offset < BLOCK_SIZE ){
                InternalKey *ink_ptr = reinterpret_cast<InternalKey*>(buf_+slot_offset);
                if(!ink_ptr->IsVaild()){
                   break; 
                }
                EntryRef entry;
                entry.key_off = slot_offset;
                sorted_vec.push_back(entry);
                slot_offset += IMS_PAGE_SIZE;
            }
            break;
        case static_cast<int>(PackingType::kHash):
            break;
        
        case static_cast<int>(PackingType::kKeyRange):
            int row = 0 ,col = 0;
            bool finished = true;
            while(slot_offset < BLOCK_SIZE && finished){
                while( row < IMS_PAGE_NUM){
                    slot_offset = row * IMS_PAGE_SIZE + col * SLOT_SIZE;
                    InternalKey *ink_ptr = reinterpret_cast<InternalKey*>(buf_+slot_offset);
                    if(!ink_ptr->IsVaild()){
                        finished = false;
                        break; 
                    }
                    EntryRef entry;
                    entry.key_off = slot_offset;
                    sorted_vec.push_back(entry);
                    ++row;
                }
                ++col;
            }
            break;
        default:
            pr_debug("Can't not generate sorted index");
            break;
    }

    return sorted_vec;
}