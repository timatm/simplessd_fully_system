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




AlignedBuf SstableManager::packingTable(const SkipList<Record, RecordComparator>& skiplist) const {
    switch (packing_type_) {
        case PackingType::kKeyPerPage:
            return keyPerPagePacking(skiplist);
        case PackingType::kHash:
            return keyHashPacking(skiplist);
        case PackingType::kKeyRange:
            return keyRangePacking(skiplist);
        default:
            pr_debug("PackingTable type is error");
            return {};
    }
}

// ———— 下面三個 packer：示範骨架（把你原本寫入邏輯搬進來） ————
// 假設：
// - AlignedBuf 有 data()/size 成員；MakeAlignedBlockSize() 會回傳 4096 對齊、大小 = IMS_PAGE_NUM*IMS_PAGE_SIZE 的緩衝
// - InternalKey::Encode() 產生的長度 <= IMS_PAGE_SIZE（你原本就檢查了）
// - HashModN(const InternalKey&, size_t) 已存在
// - InternalKey 具備成員 key.key_size（0 表示空槽；和你原本 hash 版本一致）

// 小工具：邊界檢查寫入
static inline void write_bytes_at(AlignedBuf& buf, size_t off, const void* src, size_t len) {
    if (off + len > buf.size) {
        throw std::runtime_error("write_bytes_at OOB: off=" + std::to_string(off) +
                                 " len=" + std::to_string(len) +
                                 " cap=" + std::to_string(buf.size));
    }
    std::memcpy(buf.data() + off, src, len);
}

// 小工具：把區間填成某個 byte 值
static inline void fill_bytes_at(AlignedBuf& buf, size_t off, uint8_t val, size_t len) {
    if (off + len > buf.size) {
        throw std::runtime_error("fill_bytes_at OOB: off=" + std::to_string(off) +
                                 " len=" + std::to_string(len) +
                                 " cap=" + std::to_string(buf.size));
    }
    std::memset(buf.data() + off, val, len);
}

AlignedBuf SstableManager::keyPerPagePacking(const SkipList<Record, RecordComparator>& skiplist) const {
    // 總大小 = IMS_PAGE_NUM * IMS_PAGE_SIZE（等於你原本 total_size）
    AlignedBuf out = MakeAlignedBlockSize();

    auto it = skiplist.GetIterator();
    it.SeekToFirst();

    size_t page = 0;
    while (it.Valid()) {
        if (page >= IMS_PAGE_NUM) {
            throw std::runtime_error("Too many records for fixed page count (IMS_PAGE_NUM)");
        }
        const InternalKey key = it.record().internal_key;
        const std::string enc = key.Encode();
        if (enc.size() != sizeof(InternalKey)) {
            throw std::runtime_error("Encoded key size is error");
        }
        const size_t page_off = page * IMS_PAGE_SIZE;
        write_bytes_at(out, page_off, enc.data(), enc.size());
        ++page;
        it.Next();
    }

    return out;
}

AlignedBuf SstableManager::keyHashPacking(const SkipList<Record, RecordComparator>& skiplist) const {
    // 按你原本算法：slots_per_page = IMS_PAGE_SIZE / sizeof(InternalKey)
    // 總 slot = IMS_PAGE_NUM * slots_per_page，總 bytes = total_slots * sizeof(InternalKey)
    // 而 IMS_PAGE_NUM*IMS_PAGE_SIZE == total_slots*sizeof(InternalKey)（等價）
    AlignedBuf out = MakeAlignedBlockSize();

    const size_t slots_per_page = IMS_PAGE_SIZE / sizeof(InternalKey);
    const size_t total_slots    = IMS_PAGE_NUM * slots_per_page;

    auto it = skiplist.GetIterator();
    it.SeekToFirst();

    while (it.Valid()) {
        const InternalKey& key = it.record().internal_key;
        const size_t slot_idx  = HashModN(key, slots_per_page);
        bool placed = false;

        for (size_t pg = 0; pg < IMS_PAGE_NUM; ++pg) {
            const size_t idx    = pg * slots_per_page + slot_idx;
            const size_t offset = idx * sizeof(InternalKey);
            if (idx >= total_slots) {
                throw std::runtime_error("Index overflow in keyHashPacking");
            }

            auto* ptr = reinterpret_cast<InternalKey*>(out.data() + offset);
            if (ptr->info.type == 0xFF) {
                *ptr = key;
                placed = true;
                break;
            }
        }

        if (!placed) {
            pr_debug("Hash key is out of slot");
            key.dump();
            throw std::runtime_error("Hash block full, cannot place key");
        }

        it.Next();
    }

    return out;
}

AlignedBuf SstableManager::keyRangePacking(const SkipList<Record, RecordComparator>& skiplist) const {
    AlignedBuf out = MakeAlignedBlockSize();

    const size_t slots_per_page = IMS_PAGE_SIZE / sizeof(InternalKey);
    const size_t total_slots    = IMS_PAGE_NUM * slots_per_page;

    auto iter = skiplist.GetIterator();
    iter.SeekToFirst();

    for (size_t slot = 0; slot < slots_per_page; ++slot) {
        for (size_t page = 0; page < IMS_PAGE_NUM; ++page) {
            if (!iter.Valid()) break;

            const size_t flat_index = page * slots_per_page + slot;
            if (flat_index >= total_slots) {
                throw std::runtime_error("Index overflow in keyRangePacking");
            }

            const size_t offset = flat_index * sizeof(InternalKey);
            auto* ptr = reinterpret_cast<InternalKey*>(out.data() + offset);
            *ptr = iter.record().internal_key;

            iter.Next();
        }
        if (!iter.Valid()) break;
    }

    return out;
}




AlignedBuf SstableManager::packingTable(std::queue<std::string> sortedLsit){
    
    switch (packing_type_) {
        case PackingType::kKeyPerPage:
            return keyPerPagePacking(sortedLsit);
        case PackingType::kHash:
            return keyHashPacking(sortedLsit);
        case PackingType::kKeyRange:
            return keyRangePacking(sortedLsit);
        default:
            pr_debug("PackingTable type is error");
            return {};
    }
}





static constexpr size_t kIKeySize = sizeof(InternalKey);

// 




AlignedBuf SstableManager::keyPerPagePacking(std::queue<std::string> sortedList) const {
    if (IMS_PAGE_SIZE < kIKeySize) {
        throw std::runtime_error("IMS_PAGE_SIZE is smaller than InternalKey size (64B)");
    }

    AlignedBuf out = MakeAlignedBlockSize(); // 大小 = IMS_PAGE_NUM * IMS_PAGE_SIZE

    size_t page = 0;
    while (!sortedList.empty()) {
        if (page >= IMS_PAGE_NUM) {
            throw std::runtime_error("Too many records for fixed page count (IMS_PAGE_NUM)");
        }

        const std::string& enc = sortedList.front();
        if (enc.size() != kIKeySize) {
            throw std::runtime_error("Queue element must be exactly 64 bytes (InternalKey)");
        }

        const size_t page_off = page * IMS_PAGE_SIZE;
        write_bytes_at(out, page_off, enc.data(), enc.size()); // 寫到頁首
        // 其餘區域已是 0xFF，不需再補

        ++page;
        sortedList.pop();
    }

    return out;
}

AlignedBuf SstableManager::keyHashPacking(std::queue<std::string> sortedList) const {
    const size_t slots_per_page = IMS_PAGE_SIZE / sizeof(InternalKey);
    if (slots_per_page == 0) {
        throw std::runtime_error("IMS_PAGE_SIZE too small for at least one InternalKey slot");
    }
    const size_t total_slots = IMS_PAGE_NUM * slots_per_page;

    AlignedBuf out = MakeAlignedBlockSize();   // 大小等同 total_slots * sizeof(InternalKey)
    std::memset(out.data(), 0, out.size);      // 與舊版一致：清 0，讓 key_size==0 當空槽

    while (!sortedList.empty()) {
        const std::string& enc = sortedList.front();
        if (enc.size() != kIKeySize) {
            throw std::runtime_error("Queue element must be exactly 64 bytes (InternalKey)");
        }

        InternalKey key{};
        key = InternalKey::Decode(enc);

        const size_t slot_idx = HashModN(key, slots_per_page);

        bool placed = false;
        for (size_t pg = 0; pg < IMS_PAGE_NUM; ++pg) {
            const size_t idx = pg * slots_per_page + slot_idx;
            if (idx >= total_slots) {
                throw std::runtime_error("Index overflow in keyHashPacking");
            }
            auto* cell = reinterpret_cast<InternalKey*>(out.data()) + idx;
            if (cell->key.key_size == 0) {
                *cell = key;    // 直接放入
                placed = true;
                break;
            }
        }

        if (!placed) {
            pr_debug("Hash key is out of slot");
            key.dump();
            throw std::runtime_error("Hash block full, cannot place key");
        }

        sortedList.pop();
    }

    return out;
}


AlignedBuf SstableManager::keyRangePacking(std::queue<std::string> sortedList) const {
    const size_t slots_per_page = IMS_PAGE_SIZE / sizeof(InternalKey);
    if (slots_per_page == 0) {
        throw std::runtime_error("IMS_PAGE_SIZE too small for at least one InternalKey slot");
    }
    const size_t total_slots = IMS_PAGE_NUM * slots_per_page;

    AlignedBuf out = MakeAlignedBlockSize();   // 大小等同 total_slots * sizeof(InternalKey)
    std::memset(out.data(), 0, out.size);      // 可選：清 0 方便除錯

    for (size_t slot = 0; slot < slots_per_page; ++slot) {
        for (size_t page = 0; page < IMS_PAGE_NUM; ++page) {
            if (sortedList.empty()) break;

            const std::string& enc = sortedList.front();
            if (enc.size() != kIKeySize) {
                throw std::runtime_error("Queue element must be exactly 64 bytes (InternalKey)");
            }

            const size_t flat_index = page * slots_per_page + slot;
            if (flat_index >= total_slots) {
                throw std::runtime_error("Index overflow in keyRangePacking");
            }

            auto* cell = reinterpret_cast<InternalKey*>(out.data()) + flat_index;

            // 同前：若 InternalKey 非 trivially copyable，建議 Decode
            // InternalKey key; key.Decode(enc); *cell = key;
            std::memcpy(cell, enc.data(), kIKeySize);

            sortedList.pop();
        }
        if (sortedList.empty()) break;
    }

    return out;
}



std::string SstableManager::generateFilename(uint32_t seq) {
    constexpr size_t max_digits = sizeof(mappingEntry{}.fileName)-1;
    std::ostringstream oss;
    oss << std::setw(max_digits) << std::setfill('0') << seq;
    auto str = oss.str();
    assert(str.size() == max_digits);
    return str;
}

void SstableManager::init() {}

void SstableManager::readSSTable(const std::string& filename,char *buffer) {
    
    if (!buffer) {
        std::cerr << "Failed to allocate buffer for reading SSTable\n";
        return;
    }

    // thread_pool_.Submit([filename, buffer, this]() {
    //     std::cout << "Reading SSTable from: " << filename << std::endl;

    //     int err = nvme_.nvme_read_sstable(filename, buffer);
    //     if (err == COMMAND_FAILED) {
    //         std::cerr << "[Thread] Failed to read SSTable: " << filename << std::endl;
    //         std::free(buffer);
    //         return;
    //     }
    //     std::cout << "[Thread] Read success: " << filename << std::endl;
    // });
    int err = nvme_.nvme_read_sstable(filename, buffer);
    if (err == COMMAND_FAILED) {
        std::cerr << "[Thread] Failed to read SSTable: " << filename << std::endl;
        std::free(buffer);
        return;
    }
    std::cout << "[Thread] Read success: " << filename << std::endl;
    std::cout << "[Main] Async read dispatched.\n";
}


void SstableManager::writeSSTable(uint8_t level, InternalKey minKey, InternalKey maxKey, AlignedBuf sstable_buffer,bool clearImmuteTable) {
    if (sstable_buffer.ptr == nullptr) {
        std::cerr << "SSTable buffer cannot be null" << std::endl;
        return;
    }

    Key rangeMinKey = minKey.key;
    Key rangeMaxKey = maxKey.key;
    std::string filename = generateFilename(sequenceNumber_.fetch_add(1));

    sstable_info info(filename, level, rangeMinKey, rangeMaxKey);
    std::cout << "Dispatching write for SSTable: " << filename << std::endl;
    info.dump();

    thread_pool_.Submit([info, buf = std::move(sstable_buffer), clearImmuteTable,this]() {
        std::cout << "[Thread] Entered thread task\n";
        int err = nvme_.nvme_write_sstable(info,buf.data());
        std::cout << "[Thread] nvme_write_sstable returned " << err << std::endl;
        if (err == COMMAND_FAILED) {
            pr_debug("[Thread] Failed to write SSTable: %s",info.filename);
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

        if (clearImmuteTable) {
            notify_done(info);
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
    sstable_mgr_->readSSTable(filename_, buf_.data());
    // sstable_mgr_->waitAllTasksDone();
    if (buf_.data() == nullptr) {
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
    if(internal_target.size() != kIKeySize) {
        pr_debug("SStable iterator seek is error target");
        pos_ = -1;
        return;
    }
    InternalKey target = InternalKey::Decode(std::string(internal_target));

    auto less_entry_than_target = [&](const EntryRef& e)->bool {
        if (e.key_off + kIKeySize > BLOCK_SIZE) return true; // 越界视为无效（排左）
        InternalKey ik;
        std::memcpy(&ik, buf_.data() + e.key_off, kIKeySize); // 避免未对齐/alias
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
    std::memcpy(&ik, buf_.data() + e.key_off, kIKeySize);
    // pr_debug("Read LPN: %lu  ,Offset: %lu",ik.value_ptr.lpn, ik.value_ptr.offset);
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
        // std::memcpy(&ik, buf_.data() + off, kIKeySize);
        ik = InternalKey::Decode(std::string(buf_.data() + off,kIKeySize));
        return ik.IsValid();
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
                ka = InternalKey::Decode(std::string(buf_.data() + a.key_off,kIKeySize));
                kb = InternalKey::Decode(std::string(buf_.data() + b.key_off, kIKeySize));
                // std::memcpy(&ka, buf_.data() + a.key_off, kIKeySize);
                // std::memcpy(&kb, buf_.data() + b.key_off, kIKeySize);
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


void SstableManager::notify_done(const sstable_info& info) noexcept {
    if (!on_write_done_) return;
    try {
        on_write_done_(info);
    } catch (const std::exception& e) {
        std::cerr << "[SstableManager] on_write_done_ threw: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "[SstableManager] on_write_done_ threw unknown exception\n";
    }
}

void SstableManager::notify_fail(const sstable_info& info, int err) noexcept {
    if (!on_write_fail_) return;
    try {
        on_write_fail_(info, err);
    } catch (const std::exception& e) {
        std::cerr << "[SstableManager] on_write_fail_ threw: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "[SstableManager] on_write_fail_ threw unknown exception\n";
    }
}
