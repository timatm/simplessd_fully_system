#include "db_api.hh"
#include "nvme_interface.hh"

#if RUNTYPE == 1
    #include "nvme_simplessd.hh"
#else
    #include "nvme_test.hh"
#endif
// #include "IMS_interface.hh"
#include "lsmtree.hh"
#include "options.hh"
#include "compaction.hh"
#include "range_query.hh"
#include <algorithm>
#include <mutex>
#include <vector>
#include <limits>
#include <cstring>



namespace {
using namespace sst_v2;

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

static inline bool BloomMayContain(const uint8_t* bloom,
                                  uint32_t bloom_bytes,
                                  const std::string& user_key,
                                  uint8_t k) {
    // bloom 不可用：保守回 true（當作“可能有”）
    if (bloom_bytes == 0 || k == 0) return true;

    const uint64_t m = static_cast<uint64_t>(bloom_bytes) * 8ull;
    const uint64_t h1 = FNV1aHash64(user_key.data(), user_key.size());
    uint64_t h2 = Mix64(h1);
    if (h2 == 0) h2 = 0x9e3779b97f4a7c15ull;

    for (uint8_t i = 0; i < k; ++i) {
        const uint64_t bit = (h1 + static_cast<uint64_t>(i) * h2) % m;
        const uint32_t byte_i = static_cast<uint32_t>(bit >> 3);
        const uint32_t bit_i  = static_cast<uint32_t>(bit & 7);
        if ((bloom[byte_i] & static_cast<uint8_t>(1u << bit_i)) == 0) return false;
    }
    return true;


    
}

// 用 index 找到可能的 page，再用 bloom slice 檢查，最後掃那一頁找 key
static bool IdxBloomLookup(const char* buf,
                           const std::string& user_key,
                           InternalKey& found) {
    SSTableSuperBlockV1 sb{};
    std::memcpy(&sb, buf, sizeof(sb));

    if (!CheckSuperBlock(sb)) return false;
    if (sb.format != kFormatIdxBloomData) return false;

    const uint32_t idx_cnt = sb.index_entry_count;
    if (idx_cnt == 0) return false;  // 空表

    const auto* index = reinterpret_cast<const SSTableIndexEntryV1*>(buf + sb.index_off);

    // binary search：找第一個 max_key >= user_key 的 page
    uint32_t lo = 0, hi = idx_cnt;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;

        std::string maxk(reinterpret_cast<const char*>(index[mid].key),
                        static_cast<size_t>(index[mid].key_size));

        if (maxk.compare(user_key) < 0) lo = mid + 1;
        else hi = mid;
    }

    if (lo >= idx_cnt) return false;

    const SSTableIndexEntryV1& ie = index[lo];
    const uint16_t page_id = ie.page_id;
    const uint16_t valid_slots = ie.valid_slots;
    if (valid_slots == 0) return false;

    // bloom slice check（negative → 直接排除整個 sstable）
    if (sb.filter_bytes_per_page > 0 && sb.bloom_bytes > 0) {
        const uint32_t slice_off =
            sb.bloom_off + static_cast<uint32_t>(page_id) * sb.filter_bytes_per_page;
        const uint32_t slice_end = slice_off + sb.filter_bytes_per_page;

        // 壞資料保守：不要 false negative，寧可當作 “可能有”
        if (slice_end <= sb.bloom_off + sb.bloom_bytes) {
            const uint8_t* slice = reinterpret_cast<const uint8_t*>(buf + slice_off);
            if (!BloomMayContain(slice, sb.filter_bytes_per_page, user_key, sb.bloom_k)) {
                return false; // ✅ bloom negative：一定不在
            }
        }
    }

    // 掃那一頁的 slots（最多 256 個）
    const uint32_t slot_size = sizeof(InternalKey);
    const uint32_t slots_per_page = sb.page_size / slot_size;
    if (valid_slots > slots_per_page) return false;

    const uint32_t page_off = sb.data_off + static_cast<uint32_t>(page_id) * sb.page_size;
    if (page_off + static_cast<uint32_t>(valid_slots) * slot_size > sb.block_size) return false;

    bool hit = false;
    InternalKey best{};

    for (uint16_t s = 0; s < valid_slots; ++s) {
        const char* p = buf + page_off + static_cast<uint32_t>(s) * slot_size;
        InternalKey ik = InternalKey::Decode(std::string(p, static_cast<size_t>(slot_size)));
        if (ik.UserKey() == user_key) {
            if (!hit || ik.info.seq > best.info.seq) {
                best = ik;
                hit = true;
            }
        }
    }

    if (!hit) return false;
    found = best;
    return true;
}

static bool IdxBloomLocateInMeta(const char* meta_buf,                      
                                uint32_t meta_bytes,                        
                                const std::string& user_key,              
                                uint32_t& out_page_off,                   
                                uint16_t& out_valid_slots,                 
                                uint32_t& out_page_size) {                                                                          
    if (meta_buf == nullptr) return false;                                   
    if (meta_bytes < sizeof(SSTableSuperBlockV1)) return false;              
                                                    
    SSTableSuperBlockV1 sb{};                                                
    std::memcpy(&sb, meta_buf, sizeof(sb));                                 
                                          
    if (!CheckSuperBlock(sb)) return false;                                 
    if (sb.format != kFormatIdxBloomData) return false;                    
          
    if (sb.page_size == 0) return false;                                    
    const uint32_t expect_meta_bytes =                                       
        static_cast<uint32_t>(sb.meta_pages) * static_cast<uint32_t>(sb.page_size); 
    if (meta_bytes < expect_meta_bytes) return false;                        
                                          
    if (sb.index_entry_count == 0) return false;                             
    if (sb.index_off + sb.index_bytes > meta_bytes) return false;            
    const uint32_t need_index_bytes =                                       
        static_cast<uint32_t>(sb.index_entry_count) * static_cast<uint32_t>(sizeof(SSTableIndexEntryV1));
    if (sb.index_bytes < need_index_bytes) return false;                    
                                                  
    const auto* index = reinterpret_cast<const SSTableIndexEntryV1*>(         
        meta_buf + sb.index_off                                               
    );                                                                                        
    uint32_t lo = 0;                                                         
    uint32_t hi = static_cast<uint32_t>(sb.index_entry_count);              
    while (lo < hi) {                                                       
        const uint32_t mid = (lo + hi) / 2;                                  

        const SSTableIndexEntryV1& e = index[mid];                            
        const uint8_t ksz = e.key_size;                                      
        if (ksz > sizeof(e.key)) return false;                                

        const std::string maxk(reinterpret_cast<const char*>(e.key),          
                               static_cast<size_t>(ksz));                    

        if (maxk.compare(user_key) < 0) lo = mid + 1;                        
        else hi = mid;                                                      
    }                                                                       
            
    if (lo >= static_cast<uint32_t>(sb.index_entry_count)) return false;      
                                                
    const SSTableIndexEntryV1& ie = index[lo];                               
    const uint16_t page_id     = ie.page_id;                                 
    const uint16_t valid_slots = ie.valid_slots;                             
    if (valid_slots == 0) return false;                                      

    // 9) valid_slots 不可超過 page 能放的 slot 數                               
    const uint32_t slot_size = static_cast<uint32_t>(sizeof(InternalKey));   
    const uint32_t slots_per_page = static_cast<uint32_t>(sb.page_size) / slot_size; 
    if (slots_per_page == 0) return false;                                
    if (valid_slots > slots_per_page) return false;                          

    // 10) bloom slice check（negative => 直接跳過整張 SSTable）                  
    if (sb.filter_bytes_per_page > 0 && sb.bloom_bytes > 0) {                 
        const uint64_t slice_off =                                           
            static_cast<uint64_t>(sb.bloom_off) +                            
            static_cast<uint64_t>(page_id) * static_cast<uint64_t>(sb.filter_bytes_per_page); 
        const uint64_t slice_end = slice_off + static_cast<uint64_t>(sb.filter_bytes_per_page); 
        const uint64_t bloom_end =                                            
            static_cast<uint64_t>(sb.bloom_off) + static_cast<uint64_t>(sb.bloom_bytes); 

        // 超界就保守：不做 negative（避免 false negative）                        
        if (slice_end <= bloom_end && slice_end <= meta_bytes) {              
            const uint8_t* slice = reinterpret_cast<const uint8_t*>(meta_buf + slice_off); 
            if (!BloomMayContain(slice, sb.filter_bytes_per_page, user_key, sb.bloom_k)) { 
                return false;  // ✅ bloom negative：一定不在                     
            }                                                                  
        }                                                                      
    }                                                                          

    // 11) 算出要讀的 data page 在 2MB block 內的 page offset                      
    //     data_off 通常 = meta_pages * page_size，所以 data_base_page=meta_pages   
    if (sb.data_off % sb.page_size != 0) return false;                        
    const uint32_t data_base_page = sb.data_off / sb.page_size;               

    out_page_off    = data_base_page + static_cast<uint32_t>(page_id);        
    out_valid_slots = valid_slots;                                            
    out_page_size   = sb.page_size;                                           
    return true;                                                              
}

static bool IdxBloomScanDataPage(const char* page_buf,                        
                                uint32_t page_size,                          
                                uint16_t valid_slots,                        
                                const std::string& user_key,                 
                                InternalKey& found) {                        
    // 0) 防呆                                                                  
    if (page_buf == nullptr) return false;                                    
    const uint32_t slot_size = static_cast<uint32_t>(sizeof(InternalKey));    
    if (page_size < slot_size) return false;                                  

    const uint32_t slots_per_page = page_size / slot_size;                    
    if (slots_per_page == 0) return false;                                    
    if (valid_slots > slots_per_page) return false;                           

    bool hit = false;                                                        
    InternalKey best{};                                                       

    // 1) 只掃有效 slots：0..valid_slots-1                                       
    for (uint16_t s = 0; s < valid_slots; ++s) {                              
        const char* p = page_buf + static_cast<uint32_t>(s) * slot_size;      

        // 你目前專案 decode 的用法：InternalKey::Decode(std::string(64bytes))    
        InternalKey ik = InternalKey::Decode(std::string(p, slot_size));      

        if (ik.UserKey() == user_key) {                                       
            // 同 user_key 多版本：挑 seq 最大                                     
            if (!hit || ik.info.seq > best.info.seq) {                        
                best = ik;                                                    
                hit = true;                                                   
            }                                                                 
        }                                                                     
    }                                                                         

    if (!hit) return false;                                                   
    found = best;                                                             
    return true;                                                              
}

// =========================
// Idx/Bloom Meta Cache
// SSTableID (filename) -> meta bytes
//
// A small, self-contained cache for index/filter metadata pages.
// - Stores metadata bytes keyed by SSTable identifier (e.g., filename)
// - Thread-safe via a single mutex
// - Evicts entries when capacity is reached (touch-counter based)
//
// Build-time knobs (compatible with the previous names):
//   IDXBF_META_CACHE_MODE
//     0 = disabled (always read from device)
//     1 = refresh mode (always read from device; cache is updated)
//     2 = read-through mode (serve from cache on hit)
//
//   IDXBF_META_CACHE_CAPACITY (or legacy IDXBF_META_CACHE_CAP)
//   IDXBF_META_CACHE_MIX_ROUNDS (or legacy IDXBF_META_CACHE_CPU_BURN)
// =========================

#ifndef IDXBF_META_CACHE_MODE
#define IDXBF_META_CACHE_MODE 2
#endif

// New macro name (preferred)
#ifndef IDXBF_META_CACHE_CAPACITY
  // Backward-compatible alias
  #ifdef IDXBF_META_CACHE_CAP
    #define IDXBF_META_CACHE_CAPACITY IDXBF_META_CACHE_CAP
  #else
    #define IDXBF_META_CACHE_CAPACITY 64u
  #endif
#endif

// New macro name (preferred)
#ifndef IDXBF_META_CACHE_MIX_ROUNDS
  // Backward-compatible alias
  #ifdef IDXBF_META_CACHE_CPU_BURN
    #define IDXBF_META_CACHE_MIX_ROUNDS IDXBF_META_CACHE_CPU_BURN
  #else
    #define IDXBF_META_CACHE_MIX_ROUNDS 1u
  #endif
#endif

namespace {

static volatile uint64_t g_idxbf_meta_cache_mix_sink = 0;

// A tiny mixing routine used for lightweight instrumentation and to avoid
// overly-aggressive compiler optimizations around hot paths.
static inline void MetaCacheMixBytes(uint32_t rounds,
                                     const void* data,
                                     size_t n) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    uint64_t x = g_idxbf_meta_cache_mix_sink;

    if (p == nullptr || n == 0) {
        for (uint32_t r = 0; r < rounds; ++r) {
            x ^= (x << 7) ^ (x >> 3) ^ 0x9e3779b97f4a7c15ull;
        }
        g_idxbf_meta_cache_mix_sink = x;
        return;
    }

    // Stride to keep work bounded while still touching the buffer.
    for (uint32_t r = 0; r < rounds; ++r) {
        for (size_t i = 0; i < n; i += 64) {
            x ^= static_cast<uint64_t>(p[i]) + 0x9e3779b97f4a7c15ull + (x << 6) + (x >> 2);
            x ^= x >> 33;
            x *= 0xff51afd7ed558ccdull;
            x ^= x >> 33;
        }
    }

    g_idxbf_meta_cache_mix_sink = x;
}

class IdxBloomMetaCache {
public:
    struct Entry {
        std::string id;          // SSTable identifier (e.g., filename)
        std::vector<char> data;  // metadata bytes
        uint64_t last_touch = 0; // monotonically increasing touch counter
    };

    explicit IdxBloomMetaCache(size_t capacity)
        : capacity_(capacity) {}

    // Returns true and copies cached bytes into dst on hit.
    bool GetCopy(const std::string& id, char* dst, size_t bytes) {
        if (dst == nullptr || bytes == 0) return false;

        std::lock_guard<std::mutex> lk(mu_);

        MetaCacheMixBytes(IDXBF_META_CACHE_MIX_ROUNDS, id.data(), id.size());

        // Linear scan over entries (simple container choice).
        for (size_t i = 0; i < entries_.size(); ++i) {
            Entry& e = entries_[i];

            MetaCacheMixBytes(IDXBF_META_CACHE_MIX_ROUNDS, &i, sizeof(i));

            if (e.data.size() == bytes && e.id.size() == id.size() && e.id == id) {
                // Touch bookkeeping
                e.last_touch = clock_++;

                // Optionally touch a small prefix for instrumentation
                const size_t mix_n = (e.data.size() < 256) ? e.data.size() : 256;
                if (mix_n > 0) {
                    MetaCacheMixBytes(IDXBF_META_CACHE_MIX_ROUNDS, e.data.data(), mix_n);
                }

                std::memcpy(dst, e.data.data(), bytes);
                return true;
            }
        }
        return false;
    }

    // Inserts/replaces an entry by copying bytes from src.
    void PutCopy(const std::string& id, const char* src, size_t bytes) {
        if (src == nullptr || bytes == 0) return;

        std::lock_guard<std::mutex> lk(mu_);

        // Remove existing entry (if any)
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].id == id) {
                entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }

        // Evict one entry if at capacity
        if (capacity_ > 0 && entries_.size() >= capacity_) {
            size_t victim = 0;
            uint64_t best = std::numeric_limits<uint64_t>::max();
            for (size_t i = 0; i < entries_.size(); ++i) {
                if (entries_[i].last_touch < best) {
                    best = entries_[i].last_touch;
                    victim = i;
                }
            }
            if (!entries_.empty()) {
                entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(victim));
            }
        }

        // Stage copy
        std::vector<char> tmp(bytes);
        std::memcpy(tmp.data(), src, bytes);

        // Optional instrumentation touch
        const size_t mix_n = (tmp.size() < 512) ? tmp.size() : 512;
        if (mix_n > 0) {
            MetaCacheMixBytes(IDXBF_META_CACHE_MIX_ROUNDS, tmp.data(), mix_n);
        }

        // Second-stage copy (keeps ownership local to the cache entry)
        std::vector<char> tmp2 = tmp;

        Entry e;
        e.id = std::string(id);
        e.data = std::move(tmp2);
        e.last_touch = clock_++;

        entries_.push_back(std::move(e));

        // Simple reordering step (keeps container dynamics non-trivial)
        if (entries_.size() > 1) {
            std::rotate(entries_.begin(), entries_.begin() + 1, entries_.end());
        }
    }

    void Clear() {
        std::lock_guard<std::mutex> lk(mu_);
        entries_.clear();
        clock_ = 1;
    }

private:
    std::mutex mu_;
    std::vector<Entry> entries_;
    size_t capacity_ = 0;
    uint64_t clock_ = 1;
};

// Single instance local to this translation unit
static IdxBloomMetaCache g_idxbf_meta_cache{static_cast<size_t>(IDXBF_META_CACHE_CAPACITY)};

template <typename NVMEPtr>
static inline bool ReadIdxBloomMetaWithCache(NVMEPtr& nvme,
                                             const std::string& sstable_id,
                                             uint32_t meta_pages,
                                             char* meta_buf,
                                             uint32_t meta_bytes) {
    if (!nvme || meta_buf == nullptr || meta_pages == 0 || meta_bytes == 0) {
        return false;
    }

#if IDXBF_META_CACHE_MODE == 0
    // Cache disabled: always read from device.
    return (*nvme).nvme_read_sstable_page(sstable_id, 0, meta_pages, meta_buf) == COMMAND_SUCCESS;

#else
    const bool hit = g_idxbf_meta_cache.GetCopy(sstable_id, meta_buf, meta_bytes);

#if IDXBF_META_CACHE_MODE == 2
    // Read-through: return immediately on hit.
    if (hit) return true;
#else
    // Refresh mode: always read from device; cache will be updated after the read.
    (void)hit;
#endif

    if ((*nvme).nvme_read_sstable_page(sstable_id, 0, meta_pages, meta_buf) != COMMAND_SUCCESS) {
        return false;
    }

    g_idxbf_meta_cache.PutCopy(sstable_id, meta_buf, meta_bytes);
    return true;
#endif
}

} // anonymous namespace




} // namespace


API::API(){
    pr_info("API initailizing ...");
    tree_ = std::make_shared<Tree>();
    lsmTree_ = std::make_unique<LSMTree>(tree_);
#if RUNTYPE == 1
    nvme_ = std::make_unique<gem5Driver>();
#else
    nvme_ = std::make_unique<MyNVMeDriver>();
#endif
    // if(RUNTYPE == 1){
    //     nvme_ = std::make_unique<gem5Driver>();
    // }
    // else{
    //     nvme_ = std::make_unique<MyNVMeDriver>();
    // }
    
    packing_ = kPackingType;
    memtable_ = std::make_unique<MemTable>();
    immutable_memtable_ = nullptr;
    logManager_ = std::make_unique<LogManager>(*nvme_);
    global_seq_ = 0;
    sstableManager_ = std::make_unique<SstableManager>(*nvme_,*lsmTree_);
    compaction_key_list_.resize(MAX_LEVEL);

    sstableManager_->set_on_write_done([this](const sstable_info& info) {
        this->OnSSTableFlushed(info);
    });
    sstableManager_->set_on_write_fail([this](const sstable_info& info, int err) {
        this->OnSSTableWriteFailed(info, err);
    });
    keyRangeCache_ = std::make_unique<ReadCache>();
    pr_info("API initailizing done ...");
}

void PrintConfig() {
    // ---- RUNTYPE 說明 ----
    const char* runtype_str =
    #if RUNTYPE == 0
        "Host environment (RUNTYPE=0)";
    #elif RUNTYPE == 1
        "SimpleSSD environment (RUNTYPE=1)";
    #else
        "UNKNOWN RUNTYPE (should be 0 or 1)";
    #endif
    ;

    const char* enable_disk_str = ENABLE_DISK ? "enabled" : "disabled";

    const char* search_pattern_str = SEARCH_PATTERN ? "Host generate" : "Device generate";  
    const char* nvme_driver_str =
    #if RUNTYPE == 0
        "Host/test NVMe driver";
    #elif RUNTYPE == 1
        "SimpleSSD NVMe backend";
    #else
        "UNKNOWN NVME driver mode";
    #endif
    ;

    // // ---- SELECT_POLICY 說明 ----
    // const char* select_policy_str = nullptr;
    // switch (SELECT_POLICY) {
    //     case 0: select_policy_str = "WROSTCASE"; break;
    //     case 1: select_policy_str = "RR";        break;
    //     case 2: select_policy_str = "LEVEL2CH";  break;
    //     case 3: select_policy_str = "MYPOLICY";  break;
    //     default: select_policy_str = "UNKNOWN";  break;
    // }

    // ---- PACKING_TYPE 說明 ----
    const char* packing_type_str = nullptr;
#ifdef PACKING_TYPE
    switch (PACKING_TYPE) {
        case 0: packing_type_str = "kKeyPerPage"; break;
        case 1: packing_type_str = "kHash";       break;
        case 2: packing_type_str = "kKeyRange";   break;
        case 3: packing_type_str = "kIndexFilter";   break;
        default: packing_type_str = "UNKNOWN";    break;
    }
#endif
    pr_info("========== MyDB Build Config ==========");
    pr_info("RUNTYPE            = %d (%s)", RUNTYPE, runtype_str);
    pr_info("ENABLE_DISK        = %d (%s)", ENABLE_DISK, enable_disk_str);
    pr_info("NVME_DRIVER        = %d (%s)", NVME_DRIVER, nvme_driver_str);
    // pr_info("SELECT_POLICY      = %d (%s)", SELECT_POLICY, select_policy_str);
#ifdef PACKING_TYPE
    pr_info("PACKING_TYPE       = %d (%s)", PACKING_TYPE, packing_type_str);
#endif
    pr_info("SEAECH_PATTERN GEN = %d (%s)", SEARCH_PATTERN, search_pattern_str);
    pr_info("READ_CACHE_SIZE    = %d  ",RANGE_KEY_CACHE_SIZE);
    pr_info("Level 0 size       = %d  ",LEVEL0_MAX);
    pr_info("Level 1 size       = %d  ",LEVEL1_MAX);
    pr_info("Level 2 size       = %d  ",LEVEL2_MAX);
    pr_info("Level 3 size       = %d  ",LEVEL3_MAX);
    pr_info("Level 4 size       = %d  ",LEVEL4_MAX);
    pr_info("Level 5 size       = %d  ",LEVEL5_MAX);
    pr_info("Level 6 size       = %d  ",LEVEL6_MAX);
    pr_info("========================================");

}

Status API::open() {
    PrintConfig();
    pr_info("Opening database...");
    uint32_t data_len = 0;
    int err = nvme_->nvme_open_DB(data_len);
    if (err != OPERATION_SUCCESS) return Status::IOError("nvme_open_DB failed");
    if (data_len == 0) return Status::Corruption("open_DB returned data_len=0");
    size_t alloc_sz = (data_len + 4095) & ~size_t(4095);
    void* buffer = nullptr;
    if (posix_memalign(&buffer, 4096, alloc_sz) != 0 || !buffer) {
        return Status::IOError("Failed to allocate buffer for open operation");
    }
    memset(buffer, 0, alloc_sz);

    err = nvme_->nvme_read_metadata(reinterpret_cast<char*>(buffer), data_len);

    if(err == OPERATION_FAILURE){
        free(buffer);
        pr_error("IMS nvme read metadata fail");
        return Status::Corruption("DB open failed");
    }
    std::string buf(reinterpret_cast<char*>(buffer), data_len);
    free(buffer);

    DB_INIT info;
    if(DB_INIT::decode(buf, info) == false){
        pr_error("DB_INIT decode fail");
        return Status::Corruption("DB_INIT decode fail");
    }      
    
    // pr_debug("open DB info");

    getLogManager()->setNextLBN(info.next_lbn);
    getLogManager()->setCurrentLBN(info.current_lbn);
    getLogManager()->setPageOffset(info.page_offset);
    getLogManager()->setByteOffset(info.byte_offset);
    getLogManager()->setFirstBlockOffset(info.first_block_offset);
    global_seq_ = info.global_seq;
    getSSTable()->setSequenceNumber(info.sstable_seq);

    if (!getLogManager()->decode(info.log_list)) {
        pr_error("LogManager decode fail");
        return Status::Corruption("LogManager decode fail");
    }

    if (!getLSMTree()->decode(info.node_list)) {
        pr_error("LSMTree decode failed");
        return Status::Corruption("LSMTree decode fail");
    }
    info.dump();
    dump_all();
    pr_info("open DB done");
    return Status::OK();
}

void API::print_result() {
    double search_pattern_ioM =
        static_cast<double>(search_pattern_io) / 1024.0 / 1024.0;

    pr_stat("================== DB experiment result ==================");
    pr_stat("Total Put() count: %u", total_put_count);
    pr_stat("Total Get() count: %u", total_get_count);
    if (total_SStable_num == 0.0) {
        pr_error("The average space utilization: N/A (no SSTables)");
    } else {
        double avg_util = total_space_util / total_SStable_num;
        pr_stat("average_space_utilization=%.4f", avg_util);
    }
    if (total_cache_read_count == 0.0) {
        pr_error("The cache miss rate: N/A (no cache read)");
    } else {
        double avg_util = total_cache_read_miss_count / total_cache_read_count;
        pr_stat("cache_miss_rate=%.4f", avg_util);
    }
    pr_stat("search_pattern_io=%.4fM", search_pattern_ioM);
    pr_stat("compaction_count=%u", compaction_count);
    pr_stat("Write SStable count trigger by compaction: %u",sstable_write_count_compaction);
    pr_stat("Write SStable count trigger by immutable: %u",sstable_write_count_immtable);
    pr_stat("Search hit in memory count: %u",search_hit_in_memory);
    pr_info("==========================================================");
}



Status API::close(){
    sstableManager_->waitAllTasksDone();
    pr_info("Closing database  .......");

    // 等待過去已經排入的 flush/compaction 都完成
    
    std::shared_ptr<MemTable> imm_to_flush;

    {
        std::lock_guard<std::mutex> lk(mu_);
        if (memtable_ && !memtable_->isEmpty()) {
            total_space_util    += memtable_->space_util();
            total_SStable_num   += 1;
            imm_to_flush = std::shared_ptr<MemTable>(std::move(memtable_));
        }
    }

    if (imm_to_flush) {
        auto minK = imm_to_flush->getMinKey();
        auto maxK = imm_to_flush->getMaxKey();
        const auto& list_ref = imm_to_flush->GetSkipList();
        auto buffer = sstableManager_->packingTable(list_ref);
        if (!buffer.data() || buffer.size == 0) {
            return Status::IOError("Packing failed");
        }
        sstableManager_->writeSSTable(0, minK, maxK, std::move(buffer), /*clearImmutableTable=*/true);
        sstable_write_count_immtable++;
    }
    sstableManager_->waitAllTasksDone();

    logManager_->flush_buffer();
    uint32_t page_offset = getLogManager()->get_page_offset();
    uint32_t byte_offset = getLogManager()->get_byte_offset();
    uint32_t first_block_offset = getLogManager()->get_first_block_offset();
    

    DB_INIT info;
    info.page_offset = page_offset;
    info.byte_offset = byte_offset;
    info.first_block_offset = first_block_offset;
    info.sstable_seq = getSSTable()->getSequenceNumber();
    info.global_seq = global_seq_;
    std::string enc_info = info.encode();

    void* buf = nullptr;
    size_t sz = enc_info.size();
    int ret = posix_memalign(&buf, 4096, enc_info.size());
    if (ret != 0) {
        pr_error("posix_memalign failed in close_DB: ret=%d, size=%zu", ret, sz);
        return Status::IOError("posix_memalign failed in close_DB");
    }
    if (sz > 0) {
        memcpy(buf, enc_info.data(), sz);
    }
    memcpy(buf, enc_info.data(), enc_info.size());

    int err = nvme_->nvme_close_DB(reinterpret_cast<uint8_t *>(buf), enc_info.size());   
    
    free(buf);
    if (err != OPERATION_SUCCESS) {
        return Status::IOError("nvme_close_DB failed");
    }
    lsmTree_->clear();
    pr_info("Close DB done");
    print_result();
    return Status::OK();
}


Status API::put(std::string key, std::string value) {
    total_put_count++;
    return put_impl(std::move(key), std::move(value), PutType::kPutByUser);
}

Status API::put_from_gc(std::string key, std::string value) {
    return put_impl(std::move(key), std::move(value), PutType::kPutByGC);
}


Status API::put_impl(std::string key ,std::string value,PutType t){
    std::shared_ptr<MemTable> imm_hold;
    InternalKey minK, maxK;
    bool need_flush = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!memtable_) memtable_ = std::make_unique<MemTable>();
        if (memtable_->memTableIsFull()) {
            total_space_util    += memtable_->space_util();
            total_SStable_num   += 1;
            imm_hold = std::shared_ptr<MemTable>(std::move(memtable_));
            immutable_memtable_ = imm_hold;              
            memtable_ = std::make_unique<MemTable>();

            minK = imm_hold->getMinKey();
            maxK = imm_hold->getMaxKey();
            need_flush = true;
        }
    }

    if (need_flush) {
        sstable_write_count_immtable++;
        const auto& list_ref = imm_hold->GetSkipList(); 
        auto buffer = sstableManager_->packingTable(list_ref);
        if ( buffer.data() == nullptr || buffer.size != BLOCK_SIZE) return Status::IOError("Packing failed");
        sstableManager_->writeSSTable(0, minK, maxK, std::move(buffer), /*clearImmuteTable=*/true);
        immutable_memtable_.reset();
    }
    compaction();
    uint32_t lpn = 0;
    uint32_t offset = 0;
    getLogManager()->getLPN(lpn, offset);
    uint64_t seq = global_seq_.fetch_add(1); 
    InternalKey internal_key(key,lpn,offset,seq,ValueType::kTypeValue);
    Record internal_value(internal_key,value);
    logManager_->writeLog(internal_value);
    memtable_->Put(internal_value);
    // if(logManager_->get_log_block_num() >= LOG_GC_THRESHOLD && t == PutType::kPutByUser){
    //     pr_error("GC running");
    //     log_garbage_collection();
    // }
    return Status::OK();
}


Status API::delete_key(std::string key ,std::string value){
   std::shared_ptr<MemTable> imm_hold;
    InternalKey minK, maxK;
    bool need_flush = false;

    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!memtable_) memtable_ = std::make_unique<MemTable>();
        if (memtable_->memTableIsFull()) {
            // 把 unique_ptr 轉成 shared_ptr
            imm_hold = std::shared_ptr<MemTable>(std::move(memtable_));
            immutable_memtable_ = imm_hold;              // 讓讀者可見
            memtable_ = std::make_unique<MemTable>();    // 新 memtable

            minK = imm_hold->getMinKey();                // by value
            maxK = imm_hold->getMaxKey();
            need_flush = true;
        }
    } // 解鎖

    if (need_flush) {
        const auto& list_ref = imm_hold->GetSkipList();  // 注意：是 const&，不是 shared_ptr
        auto buffer = sstableManager_->packingTable(list_ref);
        if ( buffer.data() == nullptr || buffer.size != BLOCK_SIZE) return Status::IOError("Packing failed");
        sstableManager_->writeSSTable(0, minK, maxK, std::move(buffer), /*clearImmuteTable=*/true);
    }
    compaction();
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

// PACKING_TYPE != kIdxBloomData(3)
#if PACKING_TYPE != 3 
Status API::get(std::string key,std::string& value){
    if(key.empty()){
        return Status::IOError("Key string is empty");
    }
    // std::cout << "Search key: " << key << std::endl;
    Key interkey(key);
    InternalKey search_key(key);
    auto result = std::optional<std::string>{};
    if(memtable_){
        result = memtable_->Get(key);
    }
    
    if(!result.has_value() && immutable_memtable_){
        result = immutable_memtable_->Get(key);
    }
    int level = 0;
    if(!result.has_value()){
        auto sstables = lsmTree_->search_key(interkey);
        char * buffer = (char *)allocateAligned(BLOCK_SIZE);
        while(!result.has_value() && !sstables.empty()){
            auto sstable = sstables.front();
            // std::cout   << "Find SStable: " << sstable->filename << "  Key range [ " << sstable->rangeMin.toString() << " ~ "
            //             << sstable->rangeMax.toString() << " ]" <<std::endl;
            sstables.pop();
            sstableManager_->readSSTable(sstable->filename,buffer);
            getSSTable()->waitAllTasksDone();
            auto keys = parse_sstable(buffer);
            auto it = keys.find(search_key);
            if(it != keys.end()){
                // it->dump();
                uint32_t lpn = it->value_ptr.lpn;
                uint32_t offset = it->value_ptr.offset;
                if(it->info.type == static_cast<uint8_t>(ValueType::kTypeDeletion) ){
                    pr_debug("Key is not found ,becasue this key has been deleted");
                    free(buffer);
                    return Status::NotFound("The key has been deleted");
                }
                auto record = logManager_->readLog(lpn, offset);
                // record->Dump();
                if(record.has_value()){
                    result = (*record).value;
                }
                else{
                    pr_error("Failed to read log for key: %s at LPN: %u, offset: %u", key.c_str(), lpn, offset);
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

#else
Status API::get(std::string key, std::string& value) {
    if (key.empty()) return Status::IOError("Key string is empty");

    // 1) memtable（要能辨識 tombstone）
    if (memtable_) {
        auto r = memtable_->get_record(key);
        if (r.has_value()) {
            if (r->internal_key.info.type == static_cast<uint8_t>(ValueType::kTypeDeletion))
                return Status::NotFound("The key has been deleted");
            value = r->value;
            return Status::OK();
        }
    }
    if (immutable_memtable_) {
        auto r = immutable_memtable_->get_record(key);
        if (r.has_value()) {
            if (r->internal_key.info.type == static_cast<uint8_t>(ValueType::kTypeDeletion))
                return Status::NotFound("The key has been deleted");
            value = r->value;
            return Status::OK();
        }
    }

    // 2) SSTable 逐個往下找（維持你原本方式）
    auto sstables = lsmTree_->search_key(Key(key));
    char* buffer = (char*)allocateAligned(BLOCK_SIZE);
    const uint32_t meta_pages = static_cast<uint32_t>(IDX_BLOOM_META_PAGES);
    const uint32_t meta_bytes = meta_pages * IMS_PAGE_SIZE;

    char* meta_buf = (char*)allocateAligned(meta_bytes);
    char* page_buf = (char*)allocateAligned(IMS_PAGE_SIZE);

    while (!sstables.empty()) {
        auto sstable = sstables.front();
        sstables.pop();

        sstableManager_->readSSTable(sstable->filename, buffer);
        getSSTable()->waitAllTasksDone();

        if (nvme_->nvme_read_sstable_page(sstable->filename,0,meta_pages ,meta_buf) != COMMAND_SUCCESS) {
            free(meta_buf);
            free(page_buf);
            return Status::IOError("Failed to read SSTable meta");
        }

        uint32_t page_off = 0;
        uint16_t valid_slots = 0;
        uint32_t page_size = 0;
        if (!IdxBloomLocateInMeta(meta_buf, meta_bytes, key, page_off, valid_slots, page_size)) {
            continue;
        }

        if (nvme_->nvme_read_sstable_page(sstable->filename, page_off,1, page_buf) != COMMAND_SUCCESS) {
            free(meta_buf);
            free(page_buf);
            return Status::IOError("Failed to read SSTable data page");
        }

        InternalKey ik{};
        if (!IdxBloomScanDataPage(page_buf, page_size, valid_slots, key, ik)) {
            continue;
        }

        if (ik.info.type == static_cast<uint8_t>(ValueType::kTypeDeletion)) {
            free(meta_buf);
            free(page_buf);
            return Status::NotFound("The key has been deleted");
        }

        auto rec = logManager_->readLog(ik.value_ptr.lpn, ik.value_ptr.offset);
        if (!rec.has_value()) {
            free(meta_buf);
            free(page_buf);
            return Status::IOError("Failed to read log for key");
        }

        value = rec->value;
        free(meta_buf);
        free(page_buf);
        return Status::OK();
     }

    free(buffer);
    return Status::NotFound("The key isn't in the DB");
}

#endif
#if PACKING_TYPE != 3 
Status API::get(std::string key,Record& rec){
    if(key.empty()){
        return Status::IOError("Key string is empty");
    }
    // std::cout << "Search key: " << key << std::endl;
    Key interkey(key);
    InternalKey search_key(key);
    auto result = memtable_->get_record(key);
    if(!result.has_value() && immutable_memtable_){
        result = immutable_memtable_->get_record(key);
    }
    int level = 0;
    if(!result.has_value()){
        auto sstables = lsmTree_->search_key(interkey);
        char * buffer = (char *)allocateAligned(BLOCK_SIZE);
        while(!result.has_value() && !sstables.empty()){
            auto sstable = sstables.front();
            // std::cout   << "Find SStable: " << sstable->filename << "  Key range [ " << sstable->rangeMin.toString() << " ~ "
            //             << sstable->rangeMax.toString() << " ]" <<std::endl;
            sstables.pop();
            sstableManager_->readSSTable(sstable->filename,buffer);
            getSSTable()->waitAllTasksDone();
            auto keys = parse_sstable(buffer);
            auto it = keys.find(search_key);
            if(it != keys.end()){
                // it->dump();
                uint32_t lpn = it->value_ptr.lpn;
                uint32_t offset = it->value_ptr.offset;
                if(it->info.type == static_cast<uint8_t>(ValueType::kTypeDeletion) ){
                    std::cout << "Key is not found ,becasue this key has been deleted" << std::endl;
                    free(buffer);
                    return Status::NotFound("The key has been deleted");
                }
                auto record = logManager_->readLog(lpn, offset);
                // record->Dump();
                if(record.has_value()){
                    result = record;
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
    rec = result.value();
    return Status::OK();
}
#else
Status API::get(std::string key,Record& rec){
    if (key.empty()) {
        return Status::IOError("Key string is empty");
    }

    Key userKey(key);
    InternalKey search_key(key);

    // ---- 1) 先查 memtable / immutable ----
    auto result = memtable_->get_record(key);
    if (!result.has_value() && immutable_memtable_) {
        result = immutable_memtable_->get_record(key);
    }
    if (result.has_value()) {
        if (result->internal_key.info.type == static_cast<uint8_t>(ValueType::kTypeDeletion)) {
            return Status::NotFound("The key has been deleted");
        }
        rec = *result;
        return Status::OK();
    }

    // ---- 2) 查 SSTables ----
    auto sstables = lsmTree_->search_key(userKey);
    if (sstables.empty()) {
        return Status::NotFound("The key isn't in the DB");
    }

    char* buffer = (char*)allocateAligned(BLOCK_SIZE);

    while (!sstables.empty()) {
        auto sstable = sstables.front();
        sstables.pop();

        sstableManager_->readSSTable(sstable->filename, buffer);
        getSSTable()->waitAllTasksDone();
        if (packing_ == PackingType::kIdxBloomData) {
            InternalKey ik{};
            if (!IdxBloomLookup(buffer, key, ik)) {
                continue;
            }
            if (ik.info.type == static_cast<uint8_t>(ValueType::kTypeDeletion)) {
                free(buffer);
                return Status::NotFound("The key has been deleted");
            }

            auto record = logManager_->readLog(ik.value_ptr.lpn, ik.value_ptr.offset);
            if (!record.has_value()) {
                pr_debug("Failed to read log for key: %s at LPN: %u, offset: %u", key.c_str(),
                         ik.value_ptr.lpn, ik.value_ptr.offset);
                free(buffer);
                return Status::IOError("Failed to read log for key");
            }

            rec = *record;
            free(buffer);
            return Status::OK();
        }

        // ---- 其他 packing：維持既有流程 ----
        auto keys = parse_sstable(buffer);
        auto it = keys.find(search_key);
        if (it == keys.end()) continue;

        if (it->info.type == static_cast<uint8_t>(ValueType::kTypeDeletion)) {
            free(buffer);
            return Status::NotFound("The key has been deleted");
        }

        auto record = logManager_->readLog(it->value_ptr.lpn, it->value_ptr.offset);
        if (!record.has_value()) {
            pr_debug("Failed to read log for key: %s at LPN: %u, offset: %u", key.c_str(),
                     it->value_ptr.lpn, it->value_ptr.offset);
            free(buffer);
            return Status::IOError("Failed to read log for key");
        }

        rec = *record;
        free(buffer);
        return Status::OK();
    }

    free(buffer);
    return Status::NotFound("The key isn't in the DB");
}
#endif

std::set<InternalKey ,SetComparator> API::parse_sstable(char* buffer) {
    size_t offset = 0;
    std::set<InternalKey ,SetComparator> keys;

    while (offset + sizeof(InternalKey) <= BLOCK_SIZE) {
        InternalKey key;
        key = InternalKey::Decode( std::string((buffer + offset) ,sizeof(InternalKey)) );
        offset += sizeof(InternalKey);
        if(key.IsValid()){
            keys.insert(key);
        }
    }

    return keys;
}

void API::dump_system() {
    std::cout << "Dumping system information..." << std::endl;
    std::cout << "SSD config:" << std::endl;
    std::cout   << "channel num: " << CHANNEL_NUM
                << ", plane num: " << PLANE_NUM
                << ", die num: " << DIE_NUM
                << ", package num: " << PACKAGE_NUM
                << ", block num: " << BLOCK_NUM
                << ", page num: " << IMS_PAGE_NUM
                << ", page size: " << IMS_PAGE_SIZE
                << std::endl;
    std::cout << "DB config:" << std::endl;
    std::cout << "Global sequence number: " << global_seq_.load() << std::endl;
    std::cout << "SSD simulator type:" << ( NVME_DRIVER == 0 ? "My sim" : "SimpleSSD sim" )<< std::endl;
    std::cout << "SStable packing strategy: " << std::endl;
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
    // dump_log_manager();
    std::cout << "All components dumped successfully." << std::endl;
};




static bool is_all_ff(const unsigned char* p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (p[i] != 0xFF) return false;
    }
    return true;
}

std::optional<std::set<std::string>> API::read_key_range(const std::string& filename) {
    if (filename.empty()) {
        pr_error("read_key_range: filename is empty");
        return std::nullopt;
    }

    void* p = nullptr;
    int rc = posix_memalign(&p, 4096, IMS_PAGE_SIZE);
    if (rc != 0 || p == nullptr) {
        pr_error("read_key_range: posix_memalign failed rc=%d", rc);
        return std::nullopt;
    }

    char* buffer = reinterpret_cast<char*>(p);
    std::memset(buffer, 0xFF, IMS_PAGE_SIZE);

    int err = nvme_->nvme_read_ssKeyRange(filename, buffer);
    if (err != 0) {
        pr_error("read_key_range: nvme_read_ssKeyRange failed for %s, err=%d",
                 filename.c_str(), err);
        free(buffer);
        return std::nullopt;
    }

    if (is_all_ff(reinterpret_cast<unsigned char*>(buffer), IMS_PAGE_SIZE)) {
        pr_error("read_key_range: page is ALL-FF (100%%) for SSTable: %s", filename.c_str());
        free(buffer);
        return std::nullopt;
    }

    auto keys = parse_sstable_page(buffer);
    free(buffer);

    if (keys.empty()) {
        pr_error("read_key_range: parsed keys empty for SSTable: %s (data not InternalKey?)",
                 filename.c_str());
        return std::nullopt;
    }

    std::set<std::string> user_key_range;
    for (const auto& ik : keys) {
        if (ik.key.key_size == 0) continue;
        if (!ik.IsValid()) continue;
        user_key_range.insert(ik.UserKey());
    }

    if (user_key_range.empty()) {
        pr_error("read_key_range: user_key_range empty after filtering for %s", filename.c_str());
        return std::nullopt;
    }

    return user_key_range;
}


SearchPatternD API::generate_SearchPatternD(const std::string& filename,
                                           const Key& searchKey,
                                           const std::set<std::string>& keys) {
    if (filename.empty()) throw std::invalid_argument("Filename cannot be empty");
    if (filename.size() != 35) throw std::invalid_argument("sstable_name must be 35 bytes");
    if (keys.empty()) throw std::invalid_argument("Keys set cannot be empty");
    if (searchKey.key_size == 0) throw std::invalid_argument("Search key cannot be empty");

    auto it = keys.upper_bound(searchKey.toString());

    size_t slot_index = 0;
    if (it == keys.begin()) {
        // searchKey < smallest
        slot_index = 0;
    } else if (it == keys.end()) {
        // searchKey >= largest
        slot_index = keys.size() - 1;
    } else {
        slot_index = static_cast<size_t>(std::distance(keys.begin(), std::prev(it)));
    }

    SearchPatternD pattern;
    pattern.slot_index = static_cast<uint32_t>(slot_index);
    pattern.sstable_name = filename;
    return pattern;
}


SearchPatternH API::generate_SearchPatternH(const std::string& filename,
                                           const Key& searchKey,
                                           const std::set<std::string>& keys) {
    if (filename.empty()) throw std::invalid_argument("Filename cannot be empty");
    if (filename.size() != 35) throw std::invalid_argument("sstable_name must be 35 bytes");
    if (keys.empty()) throw std::invalid_argument("Keys set cannot be empty");
    if (searchKey.key_size == 0) throw std::invalid_argument("Search key cannot be empty");

    auto it = keys.upper_bound(searchKey.toString());

    size_t slot_index = 0;
    if (it == keys.begin()) {
        slot_index = 0;
    } else if (it == keys.end()) {
        slot_index = keys.size() - 1;
    } else {
        slot_index = static_cast<size_t>(std::distance(keys.begin(), std::prev(it)));
    }

    const size_t num_slots = IMS_PAGE_SIZE / SLOT_SIZE;
    if (num_slots == 0 || IMS_PAGE_SIZE % SLOT_SIZE != 0) {
        throw std::logic_error("Invalid IMS_PAGE_SIZE / SLOT_SIZE configuration");
    }

    // 這裡要比的是 num_slots（因為你要塞進一個 page pattern）
    if (slot_index >= num_slots) {
        // ⚠️ 如果這個常發生，代表 keys 不是「一個 page 的 keys」，而是更大範圍的 keys
        throw std::out_of_range("slot_index is out of range for one page");
    }

    const std::string enc = searchKey.encode();
    if (enc.empty()) throw std::invalid_argument("Encoded search key is empty");
    if (enc.size() > SLOT_SIZE) throw std::length_error("Encoded key doesn't fit into one SLOT");

    std::string search_pattern(IMS_PAGE_SIZE, static_cast<char>(0xFF));
    std::memcpy(search_pattern.data() + slot_index * SLOT_SIZE, enc.data(), enc.size());

    SearchPatternH pattern;
    pattern.sstable_name = filename;
    pattern.search_pattern = std::move(search_pattern);
    return pattern;
}


std::set<InternalKey ,SetComparator> API::parse_sstable_page(char* buffer) {
    size_t offset = 0;
    std::set<InternalKey ,SetComparator> keys;

    while (offset + sizeof(InternalKey) <= IMS_PAGE_SIZE) {
        InternalKey key = InternalKey::Decode(buffer + offset);
        offset += sizeof(InternalKey);

        if (key.key.key_size == 0) continue;                 // 空槽
        if (!key.IsValid()) continue;                        // 防止 key_size=0xFF 這種
        if (key.info.type == INVALID_KEY_TYPE) continue;     // 你的原本判斷

        keys.insert(key);
    }
    return keys;
}

#if (SEARCH_PATTERN == 0)
Status API::search(std::string key ,std::string& value){
    total_get_count++;
    if(key.empty()){
        return Status::IOError("Key string is empty");
    }
    pr_debug("Search key: %s", key.c_str());
    Key userKey(key);
    InternalKey internalKey(key);
    // if (memtable_) {
    //     auto result = memtable_->Get(key);
    //     if (!result.has_value() && immutable_memtable_) {
    //         result = immutable_memtable_->Get(key);
    //     }
    //     if (result.has_value()) {
    //         value = *result;
    //         return Status::OK();
    //     }
    // }
    if (memtable_) {
        auto r = memtable_->get_record(key);
        if (r.has_value()) {
            search_hit_in_memory++;
            if (r->internal_key.info.type == static_cast<uint8_t>(ValueType::kTypeDeletion))
                return Status::OK();
            return Status::OK();
        }
    }
    if (immutable_memtable_) {
        search_hit_in_memory++;
        auto r = immutable_memtable_->get_record(key);
        if (r.has_value()) {
            if (r->internal_key.info.type == static_cast<uint8_t>(ValueType::kTypeDeletion))
                return Status::OK();
            return Status::OK();
        }
    }

    auto sstables = lsmTree_->search_key(userKey);
    
    
    if (sstables.empty()){
        pr_debug("No candidate SSTables found for key: %s", key.c_str());
        return Status::OK();
    }
    SearchPackageD search_package;
    if(packing_ == PackingType::kIdxBloomData){
        auto sstables = lsmTree_->search_key(Key(key));
        // char* buffer = (char*)allocateAligned(BLOCK_SIZE);
        const uint32_t meta_pages = static_cast<uint32_t>(IDX_BLOOM_META_PAGES);
        const uint32_t meta_bytes = meta_pages * IMS_PAGE_SIZE;

        char* meta_buf = (char*)allocateAligned(meta_bytes);
        char* page_buf = (char*)allocateAligned(IMS_PAGE_SIZE);

        while (!sstables.empty()) {
            auto sstable = sstables.front();
            sstables.pop();

            // sstableManager_->readSSTable(sstable->filename, buffer);
            // getSSTable()->waitAllTasksDone();

            if (!ReadIdxBloomMetaWithCache(nvme_, sstable->filename, meta_pages, meta_buf, meta_bytes)) { 
                free(meta_buf);
                free(page_buf);
                return Status::IOError("Failed to read SSTable meta");
            }

            uint32_t page_off = 0;
            uint16_t valid_slots = 0;
            uint32_t page_size = 0;
            if (!IdxBloomLocateInMeta(meta_buf, meta_bytes, key, page_off, valid_slots, page_size)) {
                continue;
            }

            if (nvme_->nvme_read_sstable_page(sstable->filename, page_off,1, page_buf) != COMMAND_SUCCESS) {
                free(meta_buf);
                free(page_buf);
                return Status::IOError("Failed to read SSTable data page");
            }

            InternalKey ik{};
            if (!IdxBloomScanDataPage(page_buf, page_size, valid_slots, key, ik)) {
                continue;
            }

            if (ik.info.type == static_cast<uint8_t>(ValueType::kTypeDeletion)) {
                free(meta_buf);
                free(page_buf);
                return Status::NotFound("The key has been deleted");
            }
            free(meta_buf);
            free(page_buf);
            return Status::OK();
        }
        free(meta_buf);
        free(page_buf);
        // free(buffer);
    }
    else{
        uint32_t index = HashModN(internalKey, SLOT_NUM_PER_PAGE);
        pr_debug("Search key: %s", key.c_str());
        // pr_info("This search run has %d candidate",sstables.size());
        while( !sstables.empty() ){
            auto sstable = sstables.front();
            // pr_error("Search SStable: %s  Level:%d Key range[ %s ~ %s ]",sstable->filename.c_str(),sstable->levelInfo,sstable->rangeMin.toString().c_str(),sstable->rangeMax.toString().c_str());

            sstables.pop();
            SearchPatternD pattern_info;
            switch (packing_){
                case PackingType::kKeyPerPage:{
                    pattern_info.slot_index = 0;
                    break;
                }
                case PackingType::kHash:{
                    pattern_info.slot_index = index;
                    break;
                }
                case PackingType::kKeyRange: {
                    try {
                        auto key_range = keyRangeCache_->get(sstable->filename);
                        total_cache_read_count++;
                        if (!key_range.has_value() || key_range->empty()) {
                            total_cache_read_miss_count++;
                            auto fresh = read_key_range(sstable->filename);
                            if (!fresh.has_value() || fresh->empty()) {
                                pr_error("KeyRange is empty after read: %s", sstable->filename.c_str());
                                return Status::Corruption("KeyRange empty: " + sstable->filename);
                            }

                            keyRangeCache_->put(sstable->filename, *fresh);
                            key_range = std::move(fresh);
                        }

                        pattern_info = generate_SearchPatternD(sstable->filename, userKey, *key_range);
                    } catch (const std::exception& e) {
                        pr_error("Build KeyRange pattern failed for %s: %s",
                                sstable->filename.c_str(), e.what());
                        return Status::IOError(std::string("Build KeyRange pattern failed: ") + e.what());
                    }
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
        search_package.search_key = userKey.toString();
        search_package.header.pattern_num = static_cast<uint32_t>(search_package.searchPatterns.size());
        const std::string encoded_package = search_package.encode();
        if (encoded_package.empty()) {
            return Status::IOError("Encoded search package is empty");
        }
        search_pattern_io += encoded_package.size();
        // search_package.dump();
        void *buffer;
        int rc = posix_memalign(&buffer, 4096, encoded_package.size());
        if (rc != 0) {
            pr_error("posix_memalign failed in API::search, rc=%d", rc);
            return Status::IOError("posix_memalign failed");
        }
        memcpy(buffer, encoded_package.data(), encoded_package.size());

        int err = nvme_->nvme_search(reinterpret_cast<char*>(buffer), encoded_package.size());
        if(err != OPERATION_SUCCESS){
            pr_error("nvme_search failed in API");
        }
        free(buffer);
    }
    return Status::OK();
}
#elif (SEARCH_PATTERN == 1)
Status API::search(std::string key ,std::string& value){
    if(key.empty()){
        return Status::IOError("Key string is empty");
    }
    pr_debug("Search key: %s", key.c_str());
    Key userKey(key);
    InternalKey internalKey(key);
    if (memtable_) {
        auto result = memtable_->Get(key);
        if (!result.has_value() && immutable_memtable_) {
            result = immutable_memtable_->Get(key);
        }
        if (result.has_value()) {
            value = *result;
            return Status::OK();
        }
    }

    auto sstables = lsmTree_->search_key(userKey);
    if (sstables.empty()){
        pr_error("No candidate SSTables found for key: %s", key.c_str());
        return Status::OK();
    }
    SearchPackageH search_package;


    while( !sstables.empty() ){
        auto sstable = sstables.front();
        pr_debug("Find SStable: %s  Key range [ %s ~ %s ]",sstable->filename.c_str(),sstable->rangeMin.toString().c_str(),sstable->rangeMax.toString().c_str());
        sstables.pop();
        SearchPatternH pattern_info;
        switch (packing_){
            case PackingType::kKeyPerPage:{
                std::string enc = userKey.encode();
                pattern_info.search_pattern = std::string(IMS_PAGE_SIZE, static_cast<char>(0xFF));
                memcpy(pattern_info.search_pattern.data(), enc.data(), enc.size());
                break;
            }
            case PackingType::kHash:{
                std::string enc = userKey.encode();
                pattern_info.search_pattern = std::string(IMS_PAGE_SIZE, static_cast<char>(0xFF));
                size_t slot_index = HashModN(internalKey, SLOT_NUM_PER_PAGE); 
                memcpy(pattern_info.search_pattern.data() + slot_index*SLOT_SIZE, enc.data(), enc.size());
                break;
            }
                
            case PackingType::kKeyRange: {
                try {
                    auto key_range = keyRangeCache_->get(sstable->filename);

                    if (!key_range.has_value() || key_range->empty()) {
                        std::set<std::string> fresh = read_key_range(sstable->filename);
                        if (fresh.empty()) {
                            pr_error("KeyRange is empty after read: %s", sstable->filename.c_str());
                            return Status::Corruption("KeyRange empty: " + sstable->filename);
                        }

                        keyRangeCache_->put(sstable->filename, fresh);
                        key_range = std::move(fresh);
                    }

                    pattern_info = generate_SearchPatternD(sstable->filename, userKey, *key_range);
                } catch (const std::exception& e) {
                    pr_error("Build KeyRange pattern failed for %s: %s",
                            sstable->filename.c_str(), e.what());
                    return Status::IOError(std::string("Build KeyRange pattern failed: ") + e.what());
                }
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
    search_package.search_key = userKey.toString();
    search_package.header.pattern_num = static_cast<uint32_t>(search_package.searchPatterns.size());
    const std::string encoded_package = search_package.encode();

    if (encoded_package.empty()) {
        return Status::IOError("Encoded search package is empty");
    }
    search_pattern_io += encoded_package.size();
    void *buffer;
    int rc = posix_memalign(&buffer, 4096, encoded_package.size());
    if (rc != 0) {
        pr_error("posix_memalign failed in API::search, rc=%d", rc);
        return Status::IOError("posix_memalign failed");
    }
    memcpy(buffer, encoded_package.data(), encoded_package.size());

    nvme_->nvme_search(reinterpret_cast<char*>(buffer), encoded_package.size());


    free(buffer);
    
    return Status::OK();
}

#endif

Status API::range_query(std::string start_key,
                        std::string end_key,
                        std::set<std::string>& result_set) {
    if (start_key.empty() || end_key.empty()) {
        return Status::InvalidArgument("Start or end key string is empty");
    }
    if (start_key > end_key) {
        return Status::InvalidArgument("Start key cannot be greater than end key");
    }

    const std::string lower =InternalKey(start_key, 0, ValueType::kTypeMin).Encode();
    const std::string upper =InternalKey(end_key, UINT64_MAX, ValueType::kTypeMax).Encode();

    std::unique_ptr<MemTableIterator> memIter;
    std::unique_ptr<MemTableIterator> immuteIter;

    if (memtable_) {
        // 且 MemTableIterator(list, const InternalKeyComparator*) 簽名成立
        memIter = std::make_unique<MemTableIterator>(memtable_->GetSkipList(), &icmp_);
    }
    if (immutable_memtable_) {
        immuteIter = std::make_unique<MemTableIterator>(immutable_memtable_->GetSkipList(), &icmp_);
    }
 
    QueryIterator it(getSSTable(),
                     getLogManager(),
                     getLSMTree(),
                     &icmp_,
                     std::move(memIter),
                     std::move(immuteIter));

    it.SetInternalRange(lower, upper);
    Status s = it.Init();
    if (!s.ok()) return s;

    for (; it.Valid(); it.Next()) {
        std::string val;
        Status sv = it.ReadValue(val);
        if (!sv.ok()) {
            std::cout << sv.ToString() << std::endl;
            return sv;
        }
        InternalKey ik = InternalKey::Decode(std::string(it.key()));

        std::cout << "KEY: " << ik.UserKey() << "[seq: " << ik.info.seq << "] " << " -> VAL: " << val << "\n";
    }

    return Status::OK();
}

Status API::scan(std::string start_key,
                        int count,
                        std::set<std::string>& result_set) {
    if (start_key.empty()) {
        return Status::InvalidArgument("Start or end key string is empty");
    }
    const std::string lower = InternalKey(start_key, 0, ValueType::kTypeMin).Encode();
    std::unique_ptr<MemTableIterator> memIter;
    std::unique_ptr<MemTableIterator> immuteIter;

    if (memtable_) {

        memIter = std::make_unique<MemTableIterator>(memtable_->GetSkipList(), &icmp_);
    }
    if (immutable_memtable_) {
        immuteIter = std::make_unique<MemTableIterator>(immutable_memtable_->GetSkipList(), &icmp_);
    }
 
    QueryIterator it(getSSTable(),
                     getLogManager(),
                     getLSMTree(),
                     &icmp_,         // 若 icmp_ 本來就是指標，這裡改成 icmp_
                     std::move(memIter),
                     std::move(immuteIter));

    // 5) 設定區間 + Init
    std::optional<std::string> lower_opt = lower;
    std::optional<std::string> upper_opt = std::nullopt;
    it.SetInternalRange(std::move(lower_opt), std::move(upper_opt));
    Status s = it.Init();
    if (!s.ok()) return s;

    for (; it.Valid(); it.Next()) {
        if(count == 0) break;
        std::string val;
        Status sv = it.ReadValue(val);
        if (!sv.ok()) {
            std::cout << sv.ToString() << std::endl;
            return sv;
        }
        result_set.insert(val);
        --count;
    }

    return Status::OK();
}

Status API::removeSSable(std::shared_ptr<TreeNode> rm){
    std::string filename = rm->filename;
    getSSTable()->eraseSSTable(filename);
    getLSMTree()->remove_sstable(rm);
    return Status::OK();
}

void API::SimulateDeviceIOIfNeeded(const std::vector<std::shared_ptr<TreeNode>> &srcNodes
                                                    ,const std::vector<std::shared_ptr<TreeNode>> &dstNodes) {

    CompactionIOSimMeta meta;
    meta.src_files.reserve(srcNodes.size());
    meta.dst_files.reserve(dstNodes.size());

    for (const auto& n : srcNodes) {
        meta.src_files.push_back(n->filename);
    }
    for (const auto& n : dstNodes) {
        meta.dst_files.push_back(n->filename);
    }
    int rc = nvme_->nvme_compaction_io(meta);
    if (rc != COMMAND_SUCCESS) {
        pr_error("SimulateDeviceIOIfNeeded: nvme_compaction_io failed (%d)", rc);
    }
}


void API::compaction() {
    auto LowerSentinel = [](const std::string& uk) {
        return InternalKey(uk, UINT64_MAX, ValueType::kTypeMin);
    };
    auto UpperSentinel = [](const std::string& uk) {
        return InternalKey(uk,0,ValueType::kTypeMax);
    };

    bool compaction = false;
    // ---------- L0 -> L1 ----------
    if (getLSMTree()->get_level_num(0) >= LEVEL0_MAX) {
        compaction_count++;
        pr_debug("Compaction start tree info:");
        // lsmTree_->dump_lsmtere();
        compaction = true;
        pr_debug("Compaction triggered at Level 0");
        auto node = getLSMTree()->findLevel0Older();
        if (!node) return;

        pr_debug("Dump compaction source info:");
        // node->dump();

        // 來源/目的候選：vector
        auto srcNodes = getLSMTree()->search_one_level(0, node->rangeMin, node->rangeMax);

        
        Key srcMin = node->rangeMin;
        Key srcMax = node->rangeMax;
        

        for (const auto& srcNode : srcNodes) {
            if(compareKey(srcNode->rangeMin,srcMin) < 0){
                srcMin = srcNode->rangeMin;
            }
            if(compareKey(srcNode->rangeMax,srcMax) > 0){
                srcMax = srcNode->rangeMax;
            }
        }
        InternalKey srcMinKey = UpperSentinel(srcMin.toString());
        InternalKey srcMaxKey = LowerSentinel(srcMax.toString());


        auto dstNodes = getLSMTree()->search_one_level(1, srcMin, srcMax);
        SimulateDeviceIOIfNeeded(srcNodes,dstNodes);
        pr_debug("Dump source nodes info:");
        // for(auto srcNode : srcNodes){
            
        //     srcNode->dump();
        // }
        pr_debug("Dump source nodes end");
        pr_debug("Dump destination nodes info:");
        // for(auto dstNode : dstNodes){
            
        //     dstNode->dump();   
        // }
        pr_debug("Dump destination nodes end");

        Key dstMinUser = node->rangeMin, dstMaxUser = node->rangeMax;
        bool hasDst = false;
        for (const auto& sp : dstNodes) {
            if (!sp) continue;
            if (!hasDst) {
                dstMinUser = sp->rangeMin;
                dstMaxUser = sp->rangeMax;
                hasDst = true;
            } else {
                if (compareKey(sp->rangeMin, dstMinUser) < 0) dstMinUser = sp->rangeMin;
                if (compareKey(sp->rangeMax, dstMaxUser) > 0) dstMaxUser = sp->rangeMax;
            }
        }

        InternalKey dstMinKey = LowerSentinel(dstMinUser.toString());
        InternalKey dstMaxKey = UpperSentinel(dstMaxUser.toString());

       

        CompactionRunner compaction(sstableManager_.get(), logManager_.get(),
                                    lsmTree_.get(), &icmp_, packing_,0,
                                    srcNodes,dstNodes,sstable_write_count_compaction);
        Status s = compaction.Run();
        if (s.ok()) {
            set_compaction_key_list(srcMaxKey, 0);
            for (const auto& sp : dstNodes) if (sp) removeSSable(sp);
            for (const auto& sp : srcNodes) if (sp) removeSSable(sp);
        } else {
            pr_error("Compaction in level0 fail");
            return;
        }
    }

    // ---------- Lk -> Lk+1 ----------
    for (int level = 1; level < MAX_LEVEL; ++level) {
        if (!compactionTrigger(level)) continue;
        compaction_count++;
        pr_debug("Compaction triggered at Level %d", level);
        pr_debug("Compaction start tree info:");
        // lsmTree_->dump_lsmtere();
        compaction = true;
        
        auto &optKey = compaction_key_list_[level];
        if (!optKey.has_value()) {
            auto firstNode = getLSMTree()->getLevelFirstNode(level);
            if (!firstNode) continue;
            optKey = LowerSentinel(firstNode->rangeMin.toString());
        }
        Key nextKey(optKey->UserKey());
        auto srcNode = getLSMTree()->getNextNode(level, nextKey);
        if (!srcNode) {
            pr_debug("No next node at level %d", level);
            continue;
        }
        std::vector<std::shared_ptr<TreeNode>> srcNodes;
        srcNodes.push_back(srcNode);
        pr_debug("Dump compaction source info:");
        // srcNode->dump();

        auto dstNodes = getLSMTree()->search_one_level(level + 1, srcNode->rangeMin, srcNode->rangeMax);

        InternalKey srcMinKey = LowerSentinel(srcNode->rangeMin.toString());
        InternalKey srcMaxKey = UpperSentinel(srcNode->rangeMax.toString());
        SimulateDeviceIOIfNeeded(srcNodes,dstNodes);
        Key dstMinUser = srcNode->rangeMin, dstMaxUser = srcNode->rangeMax;
        bool hasDst = false;
        for (const auto& sp : dstNodes) {
            if (!sp) continue;
            if (!hasDst) {
                dstMinUser = sp->rangeMin;
                dstMaxUser = sp->rangeMax;
                hasDst = true;
            } else {
                if (compareKey(sp->rangeMin, dstMinUser) < 0) dstMinUser = sp->rangeMin;
                if (compareKey(sp->rangeMax, dstMaxUser) > 0) dstMaxUser = sp->rangeMax;
            }
        }

        InternalKey dstMinKey = LowerSentinel(dstMinUser.toString());
        InternalKey dstMaxKey = UpperSentinel(dstMaxUser.toString());

        CompactionPlan srcPlane(level,     srcMinKey.Encode(), srcMaxKey.Encode());
        CompactionPlan dstPlane(level + 1, dstMinKey.Encode(), dstMaxKey.Encode());

        CompactionRunner compaction(sstableManager_.get(), logManager_.get(),
                                    lsmTree_.get(), &icmp_, packing_,level,
                                    srcNodes, dstNodes,sstable_write_count_compaction);
        Status s = compaction.Run();
        if (s.ok()) {
            set_compaction_key_list(srcMaxKey, level);  // 更新進度（上界哨兵）
            for (const auto& sp : dstNodes) if (sp) removeSSable(sp);
            removeSSable(srcNode);
        }
    }
    // if(compaction){
    //     pr_debug("Compaction result:");
    //     lsmTree_->dump_lsmtere();
    // }
    
}



void API::test(){
    auto node = getLSMTree()->findLevel0Older();
    if (!node) return;

    pr_debug("Dump compaction source info:");
    // node->dump();

    // 來源/目的候選：vector
    auto srcNodes = getLSMTree()->search_one_level(0, node->rangeMin, node->rangeMax);

    
    Key srcMin = node->rangeMin;
    Key srcMax = node->rangeMax;
    

    for (const auto& srcNode : srcNodes) {
        if(compareKey(srcMin,srcNode->rangeMin) < 0){
            srcMin = srcNode->rangeMin;
        }
        if(compareKey(srcMax,srcNode->rangeMax) < 0){
            srcMax = srcNode->rangeMax;
        }
    }

    Level0Iterator it(sstableManager_.get(),logManager_.get(),&icmp_,lsmTree_.get(),srcNodes,true);
    it.Init();
    it.SeekToFirst();
    while(it.Valid()){
        std::string value;
        it.ReadValue(value);
        InternalKey k = InternalKey::Decode(std::string(it.key()));
        std::cout << "Key: " << k.UserKey() << " -> Value: " << value << std::endl;
        it.Next();
    }
}


bool API::compactionTrigger(int level){
    if(level < 0 || level >= MAX_LEVEL) throw std::out_of_range("Level out of range");
    switch (level){
        case 0: return getLSMTree()->get_level_num(0) >= LEVEL0_MAX;
        case 1: return getLSMTree()->get_level_num(1) >= LEVEL1_MAX;
        case 2: return getLSMTree()->get_level_num(2) >= LEVEL2_MAX;
        case 3: return getLSMTree()->get_level_num(3) >= LEVEL3_MAX;
        case 4: return getLSMTree()->get_level_num(4) >= LEVEL4_MAX;
        case 5: return getLSMTree()->get_level_num(5) >= LEVEL5_MAX;
        case 6: return getLSMTree()->get_level_num(6) >= LEVEL6_MAX;
        default: return false;
    }
}


void API::init_compaction_key_list(){
    compaction_key_list_.clear();
    for(int i = 0 ; i < MAX_LEVEL ; i++){
        auto node = getLSMTree()->getLevelFirstNode(i);
        if(node == nullptr){
            compaction_key_list_.push_back(std::nullopt);
            continue;
        }
        InternalKey min(node->rangeMin.toString(),0,ValueType::kTypeMin);
        compaction_key_list_.push_back(min);
    }
}
void API::set_compaction_key_list(InternalKey key , int level){
    if(level < 0 || level >= MAX_LEVEL){
        throw std::out_of_range("Level out of range");
    }
    compaction_key_list_[level] = key;
}


void API::OnSSTableFlushed(const sstable_info& info) {
    std::lock_guard<std::mutex> lk(mu_);

    if (immutable_memtable_) {
        immutable_memtable_.reset();
        pr_debug("[API] Immutable memtable cleared after flush to %s",info.filename);
    }
}

void API::OnSSTableWriteFailed(const sstable_info& info, int err) {
    std::lock_guard<std::mutex> lk(mu_);
    pr_error("[API] Flush failed for %s , err= %d (keeping immutable memtable for retry)",info.filename,err);
}


void API::log_garbage_collection(){
    int gcBlockNum = GC_BLOCK_NUM;
    while(gcBlockNum > 0){
        uint32_t valid_offset = logManager_->get_first_block_offset();
        uint32_t lbn = logManager_->get_log_list_front();
        if(lbn >= LBN_NUM){
            pr_error("Invalid LBN for GC: %u", lbn);
            break;
        }
        if(valid_offset >= BLOCK_SIZE){
            pr_error("Invalid offset for GC: %u", valid_offset);
            break;
        }
        uint32_t next_block_valid_offset = 0;
        auto records = logManager_->readLogBlock(lbn,valid_offset,next_block_valid_offset);

        if (next_block_valid_offset == UINT32_MAX) {
            pr_error("GC cross-page read failed for LBN %u; abort this GC cycle to avoid data loss", lbn);
            break;
        }
        if (next_block_valid_offset >= BLOCK_SIZE) {
            pr_error("GC got invalid next_block_valid_offset=%u for LBN %u; abort", next_block_valid_offset, lbn);
            break;
        }
        if (records.empty() && next_block_valid_offset == 0) {
            pr_error("GC readLogBlock returned empty for LBN %u (offset=%u); stop to avoid data loss",
                     lbn, valid_offset);
            break;
        }
        

        Record rec;
        Status s;
        for(const auto record : records){
            if (static_cast<uint8_t>(record.internal_key.info.type) == static_cast<uint8_t>(ValueType::kTypeDeletion)) {
                pr_debug("GC skip tombstone key: %s", record.internal_key.UserKey().c_str());
                continue;
            }
            s = get(record.internal_key.UserKey(),rec);
            if(!s.ok()){
                if(s.IsNotFound()){
                    pr_debug("GC key: %s is deleted", record.internal_key.UserKey().c_str());
                    continue;
                } else {
                    pr_error("Failed to get key: %s during GC", record.internal_key.UserKey().c_str());
                    continue;
                }
            }
            // std::cout << "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" << std::endl;
            // record.Dump();
            // std::cout << "===========================================================" << std::endl;
            // rec.Dump();
            // std::cout << "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" << std::endl;
            // if(rec.internal_key.value_ptr.lpn != record.internal_key.value_ptr.lpn){
            //     pr_debug("log lpn is mismatch,LPN(%lu) in log , but LPN(%lu) in LSM tree"
            //             ,record.internal_key.value_ptr.lpn,rec.internal_key.value_ptr.lpn);
            // }
            // if(rec.internal_key.value_ptr.offset != record.internal_key.value_ptr.offset){
            //     pr_debug("log offset is mismatch,offset(%lu) in log , but offset(%lu) in LSM tree"
            //             ,record.internal_key.value_ptr.offset,rec.internal_key.value_ptr.offset);
            // }
            // if(rec.value_size != record.value_size){
            //     pr_debug("log value size is mismatch,offset(%lu) in log , but offset(%lu) in LSM tree"
            //             ,record.value_size,rec.value_size);
            // }
            bool still_live =
                rec.internal_key.value_ptr.lpn    == record.internal_key.value_ptr.lpn &&
                rec.internal_key.value_ptr.offset == record.internal_key.value_ptr.offset &&
                rec.value_size                    == record.value_size;

            if (still_live) {
                pr_info("GC rewrite live key: %s", record.internal_key.UserKey().c_str());
                Status ps = put_from_gc(record.internal_key.UserKey(), record.value); // 或 std::move(record.value)
                if (!ps.ok()) {
                    pr_error("GC put() failed for key: %s: %s",
                            record.internal_key.UserKey().c_str(), ps.ToString().c_str());
                }
            }
            else{
                pr_debug("GC key: %s has newer version, skip", record.internal_key.UserKey().c_str());
            }
        }
        

        logManager_->remove_log_front();
        logManager_->set_first_block_offset_(next_block_valid_offset);
        --gcBlockNum;
    }
}