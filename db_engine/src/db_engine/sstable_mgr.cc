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


static std::atomic<int> g_inflight_write(0);


#include "sstable_format_idxbf.hh"


static inline uint64_t FNV1aHash64(const void* ptr, size_t len) {
    const auto* p = static_cast<const unsigned char*>(ptr);
    uint64_t hash = 14695981039346656037ull;
    const uint64_t prime = 1099511628211ull;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint64_t>(p[i]);
        hash *= prime;
    }
    return hash;
}

static inline uint64_t Mix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

static inline void BloomSet(uint8_t* bloom, uint32_t bloom_bytes, uint32_t bitpos) {
    const uint32_t byte_i = bitpos >> 3;
    const uint32_t bit_i  = bitpos & 7;
    bloom[byte_i] |= static_cast<uint8_t>(1u << bit_i);
}

static inline void BloomAdd(uint8_t* bloom, uint32_t bloom_bytes, const std::string& user_key, uint8_t k) {
    if (bloom_bytes == 0 || k == 0) return;

    const uint64_t m = static_cast<uint64_t>(bloom_bytes) * 8ull; // bits
    const uint64_t h1 = FNV1aHash64(user_key.data(), user_key.size());
    uint64_t h2 = Mix64(h1);
    if (h2 == 0) h2 = 0x9e3779b97f4a7c15ull; // 避免退化

    for (uint8_t i = 0; i < k; ++i) {
        const uint64_t bit = (h1 + static_cast<uint64_t>(i) * h2) % m;
        BloomSet(bloom, bloom_bytes, static_cast<uint32_t>(bit));
    }
}


AlignedBuf SstableManager::packingTable(const SkipList<Record, RecordComparator>& skiplist) const {
    switch (packing_type_) {
        case PackingType::kKeyPerPage:
            return keyPerPagePacking(skiplist);
        case PackingType::kHash:
            return keyHashPacking(skiplist);
        case PackingType::kKeyRange:
            return keyRangePacking(skiplist);
        case PackingType::kIdxBloomData:
            return idxBloomDataPacking(skiplist);
        default:
            pr_error("PackingTable type is error");
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
            pr_error("Hash key is out of slot");
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




AlignedBuf SstableManager::packingTable(std::queue<std::string> sortedList){
    
    switch (packing_type_) {
        case PackingType::kKeyPerPage:
            return keyPerPagePacking(sortedList);
        case PackingType::kHash:
            return keyHashPacking(sortedList);
        case PackingType::kKeyRange:
            return keyRangePacking(sortedList);
        case PackingType::kIdxBloomData:
            return idxBloomDataPacking(std::move(sortedList));
        default:
            pr_error("PackingTable type is error");
            return {};
    }
}





static constexpr size_t kIKeySize = sizeof(InternalKey);

// 




AlignedBuf SstableManager::keyPerPagePacking(std::queue<std::string> sortedList) const {
    if (IMS_PAGE_SIZE < kIKeySize) {
        throw std::runtime_error("IMS_PAGE_SIZE is smaller than InternalKey size (64B)");
    }

    AlignedBuf out = MakeAlignedBlockSize(); 

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
            pr_error("Hash key is out of slot");
            key.dump();
            throw std::runtime_error("Hash block full, cannot place key");
        }

        sortedList.pop();
    }

    return out;
}

AlignedBuf SstableManager::idxBloomDataPacking(std::queue<std::string> sortedList) const {
    using namespace sst_v2;

    static_assert(sizeof(InternalKey) == 64, "InternalKey must be 64 bytes");

    constexpr uint8_t  kBloomK    = static_cast<uint8_t>(IDX_BLOOM_HASH_K);
    constexpr uint32_t meta_pages = static_cast<uint32_t>(IDX_BLOOM_META_PAGES);

    const uint32_t page_size       = IMS_PAGE_SIZE;
    const uint32_t block_size      = BLOCK_SIZE;
    const uint32_t slot_size       = static_cast<uint32_t>(sizeof(InternalKey));
    const uint32_t slots_per_page  = page_size / slot_size;

    const uint32_t data_pages_cap  = IMS_PAGE_NUM - meta_pages;
    const uint32_t max_entries     = data_pages_cap * slots_per_page;

    const uint32_t entry_count = static_cast<uint32_t>(sortedList.size());
    if (entry_count > max_entries) {
        throw std::runtime_error("idxBloomDataPacking(queue): too many entries");
    }

    const uint32_t data_pages_used = (entry_count + slots_per_page - 1) / slots_per_page;
    const uint32_t index_entry_count = data_pages_used;

    const uint32_t meta_bytes  = meta_pages * page_size;
    const uint32_t index_off   = static_cast<uint32_t>(sizeof(SSTableSuperBlockV1));
    const uint32_t index_bytes = index_entry_count * static_cast<uint32_t>(sizeof(SSTableIndexEntryV1));
    const uint32_t bloom_off   = AlignUp(index_off + index_bytes, 8);
    if (bloom_off > meta_bytes) {
        throw std::runtime_error("idxBloomDataPacking(queue): meta too small");
    }
    const uint32_t bloom_bytes = meta_bytes - bloom_off;
    const uint32_t data_off    = meta_bytes;

    const uint32_t filter_region_bytes = bloom_bytes;        // 沿用 bloom_bytes 當 filter region 大小
    const uint16_t filter_bytes_per_page = static_cast<uint16_t>(filter_region_bytes / index_entry_count);  // floor
    AlignedBuf out = MakeAlignedBlockSize();
    std::memset(out.data() + bloom_off, 0, bloom_bytes);

    uint32_t i = 0;
    uint16_t page_id = 0;
    uint16_t slot_in_page = 0;
    std::string page_max_user_key;

    while (!sortedList.empty()) {
        const std::string& enc = sortedList.front();
        if (enc.size() != sizeof(InternalKey)) {
            throw std::runtime_error("idxBloomDataPacking(queue): element not 64 bytes");
        }

        // data slot: 64B raw bytes 直接拷貝（維持原本 slot layout）
        const uint32_t off = data_off + i * slot_size;
        if (off + slot_size > block_size) {
            throw std::runtime_error("idxBloomDataPacking(queue): data write OOB");
        }
        std::memcpy(out.data() + off, enc.data(), sizeof(InternalKey));

        // 需要 user_key 來更新 bloom/index，所以 decode 一次
        InternalKey ik = InternalKey::Decode(enc);
        uint16_t page_id = static_cast<uint16_t>(i / slots_per_page);
        uint32_t slice_off = bloom_off + static_cast<uint32_t>(page_id) * filter_bytes_per_page;

        BloomAdd(reinterpret_cast<uint8_t*>(out.data() + slice_off),
                filter_bytes_per_page,
                ik.UserKey(),
                kBloomK);

        page_max_user_key = ik.UserKey();

        ++i;
        ++slot_in_page;

        sortedList.pop();

        const bool end_of_page  = (slot_in_page == slots_per_page);
        const bool end_of_table = (sortedList.empty());

        if (end_of_page || end_of_table) {
            SSTableIndexEntryV1 ie{};
            ie.key_size = static_cast<uint8_t>(page_max_user_key.size());
            if (page_max_user_key.size() > sizeof(ie.key)) {
                throw std::runtime_error("idxBloomDataPacking(queue): user key > 40");
            }

            std::memset(ie.key, 0, sizeof(ie.key));
            std::memcpy(ie.key, page_max_user_key.data(), ie.key_size);
            ie.page_id     = page_id;
            ie.valid_slots = slot_in_page;

            const uint32_t ie_off = index_off + static_cast<uint32_t>(page_id) * sizeof(SSTableIndexEntryV1);
            std::memcpy(out.data() + ie_off, &ie, sizeof(ie));

            ++page_id;
            slot_in_page = 0;
        }
    }

    SSTableSuperBlockV1 sb{};
    std::memcpy(sb.magic, "SSTB", 4);
    sb.version     = 1;
    sb.format      = kFormatIdxBloomData;
    sb.meta_pages  = static_cast<uint8_t>(meta_pages);
    sb.page_size   = page_size;
    sb.block_size  = block_size;
    sb.index_off   = index_off;
    sb.index_bytes = index_bytes;
    sb.bloom_off   = bloom_off;
    sb.bloom_bytes = bloom_bytes;
    sb.data_off    = data_off;
    sb.entry_count = entry_count;
    sb.index_entry_count = index_entry_count;
    sb.bloom_k     = kBloomK;
    sb.crc32       = 0;
    sb.filter_bytes_per_page = filter_bytes_per_page;

    std::memcpy(out.data(), &sb, sizeof(sb));
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

AlignedBuf SstableManager::idxBloomDataPacking(const SkipList<Record, RecordComparator>& skiplist) const {
    using namespace sst_v2;

    static_assert(sizeof(InternalKey) == 64, "InternalKey must be 64 bytes");

    constexpr uint8_t  kBloomK    = static_cast<uint8_t>(IDX_BLOOM_HASH_K);
    constexpr uint32_t meta_pages = static_cast<uint32_t>(IDX_BLOOM_META_PAGES);

    const uint32_t page_size       = IMS_PAGE_SIZE;
    const uint32_t block_size      = BLOCK_SIZE;
    const uint32_t slot_size       = static_cast<uint32_t>(sizeof(InternalKey));
    const uint32_t slots_per_page  = page_size / slot_size;

    if (meta_pages < 1 || meta_pages >= IMS_PAGE_NUM) {
        throw std::runtime_error("IDX_BLOOM_META_PAGES invalid");
    }
    if (slots_per_page == 0) {
        throw std::runtime_error("IMS_PAGE_SIZE too small for InternalKey slots");
    }

    // ---- Pass 1: count ----
    uint32_t entry_count = 0;
    {
        auto it = skiplist.GetIterator();
        it.SeekToFirst();
        while (it.Valid()) { ++entry_count; it.Next(); }
    }

    // ---- layout basics ----
    const uint32_t meta_bytes     = meta_pages * page_size;
    const uint32_t data_off       = meta_bytes;

    const uint32_t data_pages_cap = IMS_PAGE_NUM - meta_pages;
    const uint32_t max_entries    = data_pages_cap * slots_per_page;
    if (entry_count > max_entries) {
        throw std::runtime_error("idxBloomDataPacking: too many entries for fixed 2MB SSTable");
    }

    // 空表也要能寫：index_entry_count=0, filter_bytes_per_page=0
    const uint32_t data_pages_used     = (entry_count == 0) ? 0 : (entry_count + slots_per_page - 1) / slots_per_page;
    const uint32_t index_entry_count   = data_pages_used;

    const uint32_t index_off   = static_cast<uint32_t>(sizeof(SSTableSuperBlockV1)); // 64
    const uint32_t index_bytes = index_entry_count * static_cast<uint32_t>(sizeof(SSTableIndexEntryV1)); // 48 * pages

    const uint32_t bloom_off = AlignUp(index_off + index_bytes, 8);
    if (bloom_off > meta_bytes) {
        throw std::runtime_error("idxBloomDataPacking: meta region too small (index overflow)");
    }
    const uint32_t bloom_bytes = meta_bytes - bloom_off;

    uint16_t filter_bytes_per_page = 0;
    if (index_entry_count > 0) {
        filter_bytes_per_page = static_cast<uint16_t>(bloom_bytes / index_entry_count); // floor
        if (filter_bytes_per_page == 0) {
            throw std::runtime_error("idxBloomDataPacking: bloom region too small => filter_bytes_per_page=0");
        }
    }

    // ---- allocate 2MB ----
    AlignedBuf out = MakeAlignedBlockSize();

    // bloom region 必須清 0（MakeAlignedBlockSize() 會填 0xFF）
    if (bloom_bytes > 0) {
        std::memset(out.data() + bloom_off, 0, bloom_bytes);
    }

    // ---- Pass 2: write data + build per-page bloom + build index ----
    auto it = skiplist.GetIterator();
    it.SeekToFirst();

    uint32_t i = 0;
    uint16_t slot_in_page = 0;
    std::string page_max_user_key;

    while (it.Valid()) {
        const InternalKey& ik = it.record().internal_key;

        // ✅ 一律寫 Encode() 的 bytes，避免 struct layout 風險
        const std::string enc = ik.Encode();
        if (enc.size() != sizeof(InternalKey)) {
            throw std::runtime_error("idxBloomDataPacking: InternalKey::Encode() not 64 bytes");
        }

        const uint16_t page_id = static_cast<uint16_t>(i / slots_per_page);

        // data slot
        const uint32_t off = data_off + i * slot_size;
        if (off + slot_size > block_size) {
            throw std::runtime_error("idxBloomDataPacking: data write OOB");
        }
        std::memcpy(out.data() + off, enc.data(), slot_size);

        // per-page bloom slice
        if (index_entry_count > 0) {
            const uint32_t slice_off = bloom_off + static_cast<uint32_t>(page_id) * filter_bytes_per_page;
            const uint32_t slice_end = slice_off + filter_bytes_per_page;
            if (slice_end > bloom_off + bloom_bytes) {
                throw std::runtime_error("idxBloomDataPacking: bloom slice OOB");
            }
            BloomAdd(reinterpret_cast<uint8_t*>(out.data() + slice_off),
                     filter_bytes_per_page,
                     ik.UserKey(),
                     kBloomK);
        }

        page_max_user_key = ik.UserKey();

        ++i;
        ++slot_in_page;

        it.Next();
        const bool end_of_page  = (slot_in_page == slots_per_page);
        const bool end_of_table = (!it.Valid());

        if (end_of_page || end_of_table) {
            // 寫 index entry（對應剛剛那個 page_id）
            SSTableIndexEntryV1 ie{};
            if (page_max_user_key.size() > sizeof(ie.key)) {
                throw std::runtime_error("idxBloomDataPacking: user key > 40, cannot store in index");
            }
            ie.key_size = static_cast<uint8_t>(page_max_user_key.size());
            std::memset(ie.key, 0, sizeof(ie.key));
            std::memcpy(ie.key, page_max_user_key.data(), ie.key_size);

            ie.page_id     = static_cast<uint16_t>((i - 1) / slots_per_page);
            ie.valid_slots = slot_in_page;

            const uint32_t ie_off = index_off + static_cast<uint32_t>(ie.page_id) * sizeof(SSTableIndexEntryV1);
            if (ie_off + sizeof(SSTableIndexEntryV1) > bloom_off) {
                throw std::runtime_error("idxBloomDataPacking: index overlaps bloom region");
            }
            std::memcpy(out.data() + ie_off, &ie, sizeof(ie));

            slot_in_page = 0;
        }
    }

    // ---- superblock ----
    SSTableSuperBlockV1 sb{};
    std::memcpy(sb.magic, "SSTB", 4);
    sb.version     = 1;
    sb.format      = kFormatIdxBloomData;
    sb.meta_pages  = static_cast<uint8_t>(meta_pages);

    sb.page_size   = page_size;
    sb.block_size  = block_size;

    sb.index_off   = index_off;
    sb.index_bytes = index_bytes;

    sb.bloom_off   = bloom_off;
    sb.bloom_bytes = bloom_bytes;

    sb.data_off    = data_off;
    sb.entry_count = entry_count;

    sb.index_entry_count      = static_cast<uint16_t>(index_entry_count);
    sb.bloom_k                = kBloomK;
    sb.filter_bytes_per_page  = filter_bytes_per_page;
    sb.crc32                  = 0; // 先不做

    std::memcpy(out.data(), &sb, sizeof(sb));
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
        pr_error("Failed to allocate buffer for reading SSTable");
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
        pr_error("[Thread] Failed to read SSTable: %s",filename.c_str());
        // std::free(buffer);
        return;
    }
    pr_debug("[Thread] Read success: %s",filename.c_str());
    pr_debug("[Main] Async read dispatched.");
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
    pr_debug("Dispatching write for SSTable: %s",filename.c_str());
    // info.dump();

    // thread_pool_.Submit([info, buf = std::move(sstable_buffer), clearImmuteTable,this]() {
    //     auto tid        = std::this_thread::get_id();
    //     auto tid_hash   = std::hash<std::thread::id>{}(tid);
    //     // auto t_ender    = clock::now();
    //     int now_inflight = g_inflight_write.fetch_add(1,std::memory_order_relaxed)+1;
    //     pr_info("[Thread] [ENTER] tid:%zu inflight:%d",static_cast<size_t>(tid_hash),now_inflight);

    //     pr_debug("[Thread] Entered thread task");
    //     int err = nvme_.nvme_write_sstable(info,buf.data());
    //     pr_debug("[Thread] nvme_write_sstable returned %d",err);
    //     if (err == COMMAND_FAILED) {
    //         pr_error("[Thread] Failed to write SSTable: %s",info.filename);
    //         return;
    //     }
    //     pr_debug("[Thread] Write success: %s",info.filename);

    //     auto node = std::make_shared<TreeNode>(info.filename,
    //                                         info.level,
    //                                         info.min, 
    //                                         info.max);
    //     {
    //         std::unique_lock<std::mutex> lock(tree_mutex_);
    //         lsmTree_.insert_sstable(node);
    //     }

    //     if (clearImmuteTable) {
    //         notify_done(info);
    //     }
    //     now_inflight = g_inflight_write.fetch_sub(1,std::memory_order_relaxed)-1;
    //     pr_info("[Thread] [LEAVE] tid:%zu inflight:%d",static_cast<size_t>(tid_hash),now_inflight);
    //     pr_debug("SStable( %s ) written successfully.",info.filename);
    // });
    int err = nvme_.nvme_write_sstable(info,sstable_buffer.data());
    if (err == COMMAND_FAILED) {
        pr_error("Failed to write SSTable: %s",info.filename.c_str());
        return;
    }
    pr_debug("Write success: %s",info.filename.c_str());

    auto node = std::make_shared<TreeNode>(info.filename,
                                        info.level,
                                        info.min, 
                                        info.max);
    {
        std::unique_lock<std::mutex> lock(tree_mutex_);
        lsmTree_.insert_sstable(node);
    }
    pr_debug("SStable( %s ) written successfully.",info.filename.c_str());
    pr_debug("[Main] Async write dispatched.");
}

void SstableManager::eraseSSTable(const std::string& filename) {
    if(filename.empty()){
        pr_error("DeleteSSTable filename is empty");
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
        pr_error("SStable iterator seek is error target");
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
        pr_error("ReadValue failed for key: %s", ik.UserKey().c_str());
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
        case static_cast<int>(PackingType::kIdxBloomData): {
            // 只用 superblock 的 entry_count + data_off 產生 view（先不利用 index/bloom）
            sst_v2::SSTableSuperBlockV1 sb{};
            std::memcpy(&sb, buf_.data(), sizeof(sb));
            if (!sst_v2::CheckSuperBlock(sb)) {
                pr_error("SSTB superblock invalid or not SSTB format");
                break;
            }

            const uint32_t cnt = sb.entry_count;
            const uint32_t data_off = sb.data_off;

            if (data_off + cnt * kIKeySize > BLOCK_SIZE) {
                pr_error("SSTB data region OOB");
                break;
            }

            v.reserve(cnt);
            for (uint32_t i = 0; i < cnt; ++i) {
                v.push_back(EntryRef{ static_cast<uint32_t>(data_off + i * kIKeySize) });
            }
            break;
        }

        default:
            pr_error("Unknown packing type=%d", static_cast<int>(type_));
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
