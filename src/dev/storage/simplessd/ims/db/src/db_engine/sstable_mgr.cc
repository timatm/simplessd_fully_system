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
std::string SstableManager::packingTable(std::queue<std::string> sortedLsit){
    std::string package;
    switch (packing_type_)
    {
        case 0:
            package = std::string(keyPerPagePacking(sortedLsit), BLOCK_SIZE);
            break;
        case 1:
            package = std::string(keyHashPacking(sortedLsit),BLOCK_SIZE);
            break;
        case 2:
            package = std::string(keyRangePacking(sortedLsit),BLOCK_SIZE);
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


static constexpr size_t kIKeySize = sizeof(InternalKey);

char* SstableManager::keyPerPagePacking(std::queue<std::string> sortedList) {
    const size_t total_size = IMS_PAGE_NUM * IMS_PAGE_SIZE;
    if (IMS_PAGE_SIZE < kIKeySize) {
        throw std::runtime_error("IMS_PAGE_SIZE is smaller than InternalKey size (64B)");
    }

    char* buffer = static_cast<char*>(allocateAligned(total_size));
    if (!buffer) throw std::runtime_error("Failed to allocate aligned buffer");

    // 可選：預設填充 0xFF
    std::memset(buffer, 0xFF, total_size);

    size_t page = 0;
    while (!sortedList.empty()) {
        if (page >= IMS_PAGE_NUM) {
            free(buffer);
            throw std::runtime_error("Too many records for fixed page count (IMS_PAGE_NUM)");
        }

        const std::string& enc = sortedList.front();
        if (enc.size() != kIKeySize) {
            free(buffer);
            throw std::runtime_error("Queue element must be exactly 64 bytes (InternalKey)");
        }

        char* dst = buffer + page * IMS_PAGE_SIZE;
        std::memcpy(dst, enc.data(), enc.size());

        ++page;
        sortedList.pop();
    }

    return buffer;
}

char* SstableManager::keyHashPacking(std::queue<std::string> sortedList) {
    const size_t slots_per_page = IMS_PAGE_SIZE / sizeof(InternalKey);
    if (slots_per_page == 0) {
        throw std::runtime_error("IMS_PAGE_SIZE too small for at least one InternalKey slot");
    }
    const size_t total_slots = IMS_PAGE_NUM * slots_per_page;
    const size_t block_size  = total_slots * sizeof(InternalKey);

    char* buffer = static_cast<char*>(allocateAligned(block_size));
    if (!buffer) throw std::runtime_error("Failed to allocate aligned buffer");
    // 重要：清 0 讓 key_size==0 當作「空槽」判斷
    std::memset(buffer, 0, block_size);

    while (!sortedList.empty()) {
        const std::string& enc = sortedList.front();
        if (enc.size() != kIKeySize) {
            free(buffer);
            throw std::runtime_error("Queue element must be exactly 64 bytes (InternalKey)");
        }

        InternalKey key{};
        std::memcpy(&key, enc.data(), kIKeySize);

        // 與你原本版本一致：用相同的雜湊定位「欄位/槽」
        // 若你只有 HashModN(InternalKey,size_t)，這裡會直接沿用：
        size_t slot_idx = HashModN(key, slots_per_page);

        bool placed = false;
        for (size_t pg = 0; pg < IMS_PAGE_NUM; ++pg) {
            const size_t idx    = pg * slots_per_page + slot_idx;
            InternalKey*  cell  = reinterpret_cast<InternalKey*>(buffer) + idx;
            if (cell->key.key_size == 0) {
                *cell = key;
                placed = true;
                break;
            }
        }

        if (!placed) {
            free(buffer);
            throw std::runtime_error("Hash block full, cannot place key");
        }

        sortedList.pop();
    }

    return buffer;
}

char* SstableManager::keyRangePacking(std::queue<std::string> sortedList) {
    const size_t slots_per_page = IMS_PAGE_SIZE / sizeof(InternalKey);
    if (slots_per_page == 0) {
        throw std::runtime_error("IMS_PAGE_SIZE too small for at least one InternalKey slot");
    }
    const size_t total_slots = IMS_PAGE_NUM * slots_per_page;
    const size_t block_size  = total_slots * sizeof(InternalKey);

    char* buffer = static_cast<char*>(allocateAligned(block_size));
    if (!buffer) throw std::bad_alloc();
    // 可選：非必要，但清 0 方便除錯
    std::memset(buffer, 0, block_size);

    // 與舊邏輯一致：column-major（先同 slot 橫跨所有 page，再到下一個 slot）
    for (size_t slot = 0; slot < slots_per_page; ++slot) {
        for (size_t page = 0; page < IMS_PAGE_NUM; ++page) {
            if (sortedList.empty()) break;

            const std::string& enc = sortedList.front();
            if (enc.size() != kIKeySize) {
                free(buffer);
                throw std::runtime_error("Queue element must be exactly 64 bytes (InternalKey)");
            }

            const size_t flat_index = page * slots_per_page + slot;
            InternalKey* cell = reinterpret_cast<InternalKey*>(buffer) + flat_index;

            // 直接寫入 InternalKey
            std::memcpy(cell, enc.data(), kIKeySize);

            sortedList.pop();
        }
        if (sortedList.empty()) break;
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
void SstableManager::eraseSSTable(const std::string& filename) {
    if(filename.empty()){
        pr_debug("DeleteSSTable filename is empty");
        return;
    }
    int err = nvme_.nvme_erase_sstable(filename);
}

// ---------------- Init ----------------
Status SstableIterator::Init() {
    entries_.clear();
    pos_ = -1;

    // 让 readSSTable 读满一个块；失败时返回错误
    // 例如：Status readSSTable(const std::string&, char*& out_buf); 保障 out_buf 指向 BLOCK_SIZE 的内存
    sstable_mgr_->readSSTable(filename_, buf_);
    sstable_mgr_->waitAllTasksDone();
    if (buf_ == nullptr) {
        return Status::IOError("Failed to read SSTable: " + filename_); 
    }

    entries_ = gen_sorted_view();
    st_ = Status::OK();
    return st_;
}

// -------------- Valid / First / Last --------------
bool SstableIterator::Valid() const {
    return st_.ok() && pos_ >= 0 && pos_ < static_cast<int>(entries_.size());
}
void SstableIterator::SeekToFirst() { pos_ = entries_.empty() ? -1 : 0; }
void SstableIterator::SeekToLast()  { pos_ = entries_.empty() ? -1 : static_cast<int>(entries_.size()) - 1; }

// ---------------- Seek (lower_bound) ----------------
void SstableIterator::Seek(std::string_view internal_target) {
    if (entries_.empty()) { pos_ = -1; return; }

    // 如果你的 Decode 有 length 版，建议传长度；没有也可直接 memcpy 再比较对象
    InternalKey target = InternalKey::Decode(internal_target.data());

    auto less_entry_than_target = [&](const EntryRef& e)->bool {
        if (e.key_off + kIKeySize > BLOCK_SIZE) return true; // 越界视为无效（排左）
        InternalKey ik;
        std::memcpy(&ik, buf_ + e.key_off, kIKeySize); // 避免未对齐/alias
        return (*icmp_)(ik, target); // ik < target ?
    };

    int l = 0, r = static_cast<int>(entries_.size()) - 1, ans = entries_.size();
    while (l <= r) {
        int m = (l + r) >> 1;
        if (less_entry_than_target(entries_[m])) l = m + 1;
        else { ans = m; r = m - 1; }
    }
    pos_ = (ans == static_cast<int>(entries_.size())) ? -1 : ans;
}

// ---------------- ReadValue ----------------
Status SstableIterator::ReadValue(std::string& out) const {
    if (!Valid()) return Status::IOError("invalid iter");
    const auto& e = entries_[pos_];
    if (e.key_off + kIKeySize > BLOCK_SIZE) return Status::Corruption("ikey OOB");

    InternalKey ik;
    std::memcpy(&ik, buf_ + e.key_off, kIKeySize);

    auto rec = log_mgr_->readLog(ik.value_ptr.lpn, ik.value_ptr.offset);
    if (!rec) {
        pr_debug("ReadValue failed for key: %s", ik.UserKey().c_str());
        return Status::NotFound("value not found in log for key: " + ik.UserKey());
    }
    out = std::move(rec->value);
    return Status::OK();
}


// ---------------- gen_sorted_view ----------------
std::vector<SstableIterator::EntryRef> SstableIterator::gen_sorted_view() {
    std::vector<EntryRef> v;
    if (!buf_) return v;

    auto is_valid_at = [&](size_t off)->bool {
        if (off + kIKeySize > BLOCK_SIZE) return false;
        InternalKey ik;
        std::memcpy(&ik, buf_ + off, kIKeySize);
        return ik.IsValid(); // 确认你的拼字
    };

    const int slots_per_page = IMS_PAGE_SIZE / SLOT_SIZE;

    switch (static_cast<int>(type_)) {
        case static_cast<int>(PackingType::kKeyPerPage): {
            // 每页一条，页序就是排序
            for (size_t off = 0; off + kIKeySize <= BLOCK_SIZE; off += IMS_PAGE_SIZE) {
                if (!is_valid_at(off)) break;
                v.push_back(EntryRef{ static_cast<uint32_t>(off) });
            }
            break;
        }
        case static_cast<int>(PackingType::kKeyRange): {
            // page0 slot0, page1 slot0, ... 再换 slot1（column-major）
            bool stop = false;
            for (int col = 0; col < slots_per_page && !stop; ++col) {
                for (int row = 0; row < IMS_PAGE_NUM; ++row) {
                    size_t off = static_cast<size_t>(row) * IMS_PAGE_SIZE
                               + static_cast<size_t>(col) * SLOT_SIZE;
                    if (off + kIKeySize > BLOCK_SIZE) { stop = true; break; }
                    if (!is_valid_at(off))            { stop = true; break; }
                    v.push_back(EntryRef{ static_cast<uint32_t>(off) });
                }
            }
            break;
        }
        case static_cast<int>(PackingType::kHash): {
            // 先按 bucket 收集（column-major）
            for (int bucket = 0; bucket < slots_per_page; ++bucket) {
                for (int row = 0; row < IMS_PAGE_NUM; ++row) {
                    size_t off = static_cast<size_t>(row) * IMS_PAGE_SIZE
                               + static_cast<size_t>(bucket) * SLOT_SIZE;
                    if (off + kIKeySize > BLOCK_SIZE) break;
                    if (!is_valid_at(off))            break;
                    v.push_back(EntryRef{ static_cast<uint32_t>(off) });
                }
            }
            // Hash 版式：收集后做一次全局排序
            std::sort(v.begin(), v.end(), [&](const EntryRef& a, const EntryRef& b) {
                InternalKey ka, kb;
                std::memcpy(&ka, buf_ + a.key_off, kIKeySize);
                std::memcpy(&kb, buf_ + b.key_off, kIKeySize);
                return (*icmp_)(ka, kb);
            });
            break;
        }
        default:
            pr_debug("Unknown packing type=%d", static_cast<int>(type_));
            break;
    }
    return v;
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
    return entry_key(entries_[pos_]); // 假設 entry_key 回傳 64B InternalKey 的 view
}

Status SstableIterator::status() const { return st_; }