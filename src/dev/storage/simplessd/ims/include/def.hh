#ifndef __DEF_HH__
#define __DEF_HH__
#include <optional>
#include "internal_key.hh"
#include "print.hh"
#include <cstdint>
#include <cstring>
#include <string>
#include <memory>
// [SSD setting start]

#define CHANNEL_NUM         8
#define PACKAGE_NUM         8
#define DIE_NUM             2
#define PLANE_NUM           2
#define BLOCK_NUM           64
#define IMS_PAGE_NUM        128
#define IMS_PAGE_SIZE       16384

#define BLOCK_SIZE ((IMS_PAGE_SIZE)*(IMS_PAGE_NUM))

#define LOG2_CEIL(x) ( \
    ((x) <= 1) ? 0 : \
    ((x) <= 2) ? 1 : \
    ((x) <= 4) ? 2 : \
    ((x) <= 8) ? 3 : \
    ((x) <= 16) ? 4 : \
    ((x) <= 32) ? 5 : \
    ((x) <= 64) ? 6 : \
    ((x) <= 128) ? 7 : \
    ((x) <= 256) ? 8 : \
    ((x) <= 512) ? 9 : \
    ((x) <= 1024) ? 10 : -1)

#define CHANNEL_BITS LOG2_CEIL(CHANNEL_NUM)
#define PACKAGE_BITS LOG2_CEIL(PACKAGE_NUM)
#define DIE_BITS     LOG2_CEIL(DIE_NUM)
#define PLANE_BITS   LOG2_CEIL(PLANE_NUM)
#define BLOCK_BITS   LOG2_CEIL(BLOCK_NUM)

#define MAPPINGLBN 1

constexpr double SSD_PROVISION_RATIO = 0.1;
constexpr int LBN_NUM =(int) ( (double)( (CHANNEL_NUM) * (PACKAGE_NUM) * (DIE_NUM) * (PLANE_NUM) * (BLOCK_NUM) ) * (1- SSD_PROVISION_RATIO) );
// #define LBN_NUM ( (CHANNEL_NUM) * (PACKAGE_NUM) * (DIE_NUM) * (PLANE_NUM) * (BLOCK_NUM) * (1- SSD_PROVISION_RATIO) )
#define LBN_SIZE ( (IMS_PAGE_SIZE) * (IMS_PAGE_NUM) )

#define LPN_NUM ((LBN_NUM) * (IMS_PAGE_NUM))

#define LBN2CH(lbn)       ((lbn) % (CHANNEL_NUM))
#define LBN2PACKAGE(lbn)  (((lbn) / (CHANNEL_NUM)) % (PACKAGE_NUM))
#define LBN2DIE(lbn)      (((lbn) / ((CHANNEL_NUM) * (PACKAGE_NUM))) % (DIE_NUM))
#define LBN2PLANE(lbn)    (((lbn) / ((CHANNEL_NUM) * (PACKAGE_NUM) * (DIE_NUM))) % (PLANE_NUM))



#define LBN2LPN(lbn) ((lbn) * (IMS_PAGE_NUM)) 
#define LPN2LBN(lpn) ((lpn) / (IMS_PAGE_NUM))

#define OPERATION_SUCCESS 0
#define OPERATION_FAILURE 1

static constexpr uint64_t INVALID_64 = 0xFFFFFFFFFFFFFFFFull;
static constexpr uint32_t INVALID_32 = 0xFFFFFFFFu;

#define INVALIDLBN 0xFFFFFFFFFFFFFFFF
#define INVALIDCH  0xFF
// [SSD setting end]



// [DB setting]

constexpr int LEVEL_TIMES = 2;
constexpr int LEVEL0_MAX = 8;
constexpr int LEVEL1_MAX = 10;
constexpr int LEVEL2_MAX = LEVEL1_MAX * LEVEL_TIMES;
constexpr int LEVEL3_MAX = LEVEL2_MAX * LEVEL_TIMES;
constexpr int LEVEL4_MAX = LEVEL3_MAX * LEVEL_TIMES;
constexpr int LEVEL5_MAX = LEVEL4_MAX * LEVEL_TIMES;
constexpr int LEVEL6_MAX = LEVEL5_MAX * LEVEL_TIMES;
struct RelateChInfo {
    std::vector<std::vector<int>> inter;  // inter-level impact[c]
    std::vector<int> intra;  // intra-level impact[c]
    std::vector<int> L0;
    int node_level;
};
enum class SelectT {
    WROSTCASE = 0,
    RR        = 1,
    LEVEL2CH  = 2,
    MYPOLICY  = 3
};
#ifndef SELECT_POLICY
#define SELECT_POLICY 2
#endif

// RUNTYPE          : My enviroment / SimpleSSD = 0 / 1
// ENABLE_DISK      : Disable / Enable = 0 / 1
// NVME_DRIVER      : My NVMe driver / Simplessd NVMe driver = 0 / 1  
#ifndef RUNTYPE
#define RUNTYPE 1
#endif

#if   RUNTYPE == 0
    #define ENABLE_DISK 1
    #define NVME_DRIVER 0
#elif RUNTYPE == 1
    #define ENABLE_DISK 0
    #define NVME_DRIVER 1
#else
    #error "RUNTYPE must be 0 (host) or 1 (SimpleSSD)"
#endif

// Serach pattern generate by device / Serach pattern generate by host = 0 / 1
#ifndef SEARCH_PATTERN
#define SEARCH_PATTERN 0
#endif

struct AlignedBuf {
    std::unique_ptr<void, void(*)(void*)> ptr{nullptr, &::free};
    size_t size  = 0;    // 一定是 kTableSize
    size_t align = 0;    // 一定是 kAlign

    char* data() const{ return static_cast<char*>(ptr.get()); }
    explicit operator bool() const { return ptr != nullptr; }
};

constexpr double alpha_inter_ = 1.0;
constexpr double alpha_intra_ = 1.0;

// [DB setting end]




// [IMS setting start]
#define SUPER_BLOCK 0
#define HAS_NEXT_PAGE 1
#define NO_NEXT_PAGE  0

#define MAX_LEVEL 7
// #define MAX_FILENAME_LENGTH 56 // SStable file name length
#define MAGIC 0x900118FFFEEFFFEE


#define INVALID_KEYRANGE 0xFFFFFFFF
#define INVALID_LEVEL -1
#define INVALID_CHANNEL -1
#define INVALID_KEY_TYPE 0xFF

// [lbn:4B]
// [filename_len:4B][filename_bytes]
// [levelInfo:4B]
// [channelInfo:4B]
// [min_len:4B][min_key_bytes]
// [max_len:4B][max_key_bytes]

// 給 fake compaction I/O 用的 metadata
struct CompactionIOSimMeta {
    std::vector<std::string> src_files;
    std::vector<std::string> dst_files;

    std::string encode() const {
        std::string out;

        auto append_u32 = [&](uint32_t v) {
            for (int i = 0; i < 4; ++i) {
                out.push_back(static_cast<char>((v >> (i * 8)) & 0xFF));
            }
        };

        auto append_str = [&](const std::string& s) {
            append_u32(static_cast<uint32_t>(s.size()));
            out.append(s.data(), s.size());
        };

        append_u32(static_cast<uint32_t>(src_files.size()));
        append_u32(static_cast<uint32_t>(dst_files.size()));
        for (const auto& s : src_files) append_str(s);
        for (const auto& s : dst_files) append_str(s);

        return out;
    }

    static CompactionIOSimMeta decode(const char* data, size_t len) {
        CompactionIOSimMeta meta;
        size_t off = 0;

        auto read_u32 = [&](uint32_t& v) {
            if (off + 4 > len) throw std::runtime_error("decode u32 overflow");
            v = 0;
            for (int i = 0; i < 4; ++i) {
                v |= (static_cast<uint32_t>(
                        static_cast<unsigned char>(data[off + i])) << (i * 8));
            }
            off += 4;
        };

        auto read_str = [&](std::string& s) {
            uint32_t sz = 0;
            read_u32(sz);
            if (off + sz > len) throw std::runtime_error("decode str overflow");
            s.assign(data + off, data + off + sz);
            off += sz;
        };

        uint32_t ns, nd;
        read_u32(ns);
        read_u32(nd);

        meta.src_files.resize(ns);
        meta.dst_files.resize(nd);

        for (uint32_t i = 0; i < ns; ++i) read_str(meta.src_files[i]);
        for (uint32_t i = 0; i < nd; ++i) read_str(meta.dst_files[i]);

        return meta;
    }
};


struct hostInfo {
    uint32_t    lbn;
    std::string filename;   // file name size <= 36
    int         levelInfo;
    int         channelInfo;
    Key         rangeMin;
    Key         rangeMax;

    static constexpr uint32_t MAX_FILENAME_LEN = 36;

    hostInfo()
        : lbn(INVALID_32),
          filename(),
          levelInfo(INVALID_LEVEL),
          channelInfo(INVALID_CHANNEL),
          rangeMin(),
          rangeMax() {}

    hostInfo(std::string name, int level, int ch, Key min, Key max)
        : lbn(INVALID_32),
          filename(std::move(name)),
          levelInfo(level),
          channelInfo(ch),
          rangeMin(std::move(min)),
          rangeMax(std::move(max)) {}

    hostInfo(std::string name, int level, Key min, Key max)
        : hostInfo(std::move(name), level, INVALID_CHANNEL,
                   std::move(min), std::move(max)) {}

    explicit hostInfo(std::string name)
        : hostInfo(std::move(name), INVALID_LEVEL, INVALID_CHANNEL,
                   Key{}, Key{}) {}

    void dump() const {
        std::cout << "hostInfo: filename=" << filename
                  << ", level="   << levelInfo
                  << ", channel=" << channelInfo << "\n";
        std::cout << "  minKey: ";
        rangeMin.dumpString();
        std::cout << "  maxKey: ";
        rangeMax.dumpString();
    }

    // ================= Encode =================
    //
    // layout:
    //   [lbn (4)]
    //   [fname_len (4)]
    //   [filename bytes]
    //   [levelInfo (4)]
    //   [channelInfo (4)]
    //   [min_len (4)]
    //   [rangeMin.encode() bytes (min_len)]
    //   [max_len (4)]
    //   [rangeMax.encode() bytes (max_len)]
    //
    std::string encode() const {
        std::string out;

        // 估一個大概的容量，減少 realloc
        uint32_t fname_len = static_cast<uint32_t>(filename.size());
        std::string minEnc = rangeMin.encode();
        std::string maxEnc = rangeMax.encode();
        uint32_t min_len   = static_cast<uint32_t>(minEnc.size());
        uint32_t max_len   = static_cast<uint32_t>(maxEnc.size());

        out.reserve(4 + 4 + fname_len +
                    4 + 4 +
                    4 + min_len +
                    4 + max_len);

        auto append_raw = [&](const void* ptr, size_t n) {
            out.append(reinterpret_cast<const char*>(ptr), n);
        };

        // lbn
        append_raw(&lbn, sizeof(lbn));

        // filename (len + data)
        append_raw(&fname_len, sizeof(fname_len));
        if (fname_len > 0) {
            append_raw(filename.data(), fname_len);
        }

        // levelInfo + channelInfo
        append_raw(&levelInfo,   sizeof(levelInfo));
        append_raw(&channelInfo, sizeof(channelInfo));

        // rangeMin
        append_raw(&min_len, sizeof(min_len));
        if (min_len > 0) {
            append_raw(minEnc.data(), min_len);
        }

        // rangeMax
        append_raw(&max_len, sizeof(max_len));
        if (max_len > 0) {
            append_raw(maxEnc.data(), max_len);
        }

        return out;
    }

    // ================= Safe decode (推薦用這個) =================
    //
    // 回傳 true 表示成功；false 表示 buffer 壞掉或格式不對。
    //
    static bool decode(const std::string& buf, hostInfo& info) {
        const char* data = buf.data();
        const size_t len = buf.size();
        size_t off = 0;

        auto need = [&](size_t n) -> bool {
            return off + n <= len;
        };
        auto read_raw = [&](void* dst, size_t n) -> bool {
            if (!need(n)) return false;
            std::memcpy(dst, data + off, n);
            off += n;
            return true;
        };

        info = hostInfo{};  // reset

        // lbn
        if (!read_raw(&info.lbn, sizeof(info.lbn))) {
            return false;
        }

        // filename length
        uint32_t fname_len = 0;
        if (!read_raw(&fname_len, sizeof(fname_len))) {
            return false;
        }
        if (!need(fname_len)) {
            return false;
        }
        if (fname_len > 0) {
            info.filename.assign(data + off, fname_len);
            off += fname_len;
        } else {
            info.filename.clear();
        }

        // level + channel
        if (!read_raw(&info.levelInfo,   sizeof(info.levelInfo)))   return false;
        if (!read_raw(&info.channelInfo, sizeof(info.channelInfo))) return false;

        // rangeMin
        uint32_t min_len = 0;
        if (!read_raw(&min_len, sizeof(min_len))) return false;
        if (!need(min_len)) return false;
        if (min_len > 0) {
            if (min_len < Key::ENCODED_SIZE) {
                // 格式不對，長度太短
                return false;
            }
            if (!Key::decode(data + off, min_len, info.rangeMin)) {
                return false;
            }
            off += min_len;
        } else {
            info.rangeMin = Key{};
        }

        // rangeMax
        uint32_t max_len = 0;
        if (!read_raw(&max_len, sizeof(max_len))) return false;
        if (!need(max_len)) return false;
        if (max_len > 0) {
            if (max_len < Key::ENCODED_SIZE) {
                return false;
            }
            if (!Key::decode(data + off, max_len, info.rangeMax)) {
                return false;
            }
            off += max_len;
        } else {
            info.rangeMax = Key{};
        }

        return true;
    }

    static hostInfo decodeOrThrow(const std::string& buf) {
        hostInfo info;
        if (!decode(buf, info)) {
            throw std::runtime_error("hostInfo::decodeOrThrow: invalid buffer");
        }
        return info;
    }
};




struct valueLogInfo{
    uint32_t lbn;  // This log store in which LBN
    uint32_t page_offset; // This log store in which LPN
    valueLogInfo(uint64_t l,uint64_t p):
        lbn(l),
        page_offset(p){}
};

#pragma pack(push, 1)
struct super_page{
    uint64_t magic; 
    uint64_t mapping_store;
    uint64_t mapping_page_num;
    uint64_t log_store;     
    uint64_t log_page_num;   
    uint64_t currentLogLBN;    
    uint64_t nextLogLBN;        
    uint64_t logOffset;
    uint64_t byteOffset;
    uint64_t firstBlockOffset;
    uint64_t usedLBN_num;
    uint64_t global_sequence;
    uint64_t sstable_sequence;
    uint8_t lastUsedChannel;

    static constexpr size_t header_size =
        sizeof(magic) +
        sizeof(mapping_store) +
        sizeof(mapping_page_num) +
        sizeof(log_store) +
        sizeof(log_page_num) +
        sizeof(currentLogLBN) +
        sizeof(nextLogLBN) +
        sizeof(logOffset) +
        sizeof(byteOffset) +
        sizeof(firstBlockOffset) +
        sizeof(usedLBN_num) +
        sizeof(lastUsedChannel) +
        sizeof(global_sequence) +
        sizeof(sstable_sequence);
    uint8_t reserved[IMS_PAGE_SIZE - header_size];
    super_page(uint64_t m,uint64_t mapping_store,uint64_t log_store):
        magic(m),
        mapping_store(mapping_store),
        mapping_page_num(0),
        log_store(log_store),
        log_page_num(0),
        currentLogLBN(INVALIDLBN),
        nextLogLBN(INVALIDLBN),
        logOffset(0),
        byteOffset(0),
        firstBlockOffset(0),
        usedLBN_num(0),
        lastUsedChannel(INVALIDCH){}
    void dump() {
        std::cout << "========= Super Page Dump =========" << std::endl;
        std::cout << "magic             : 0x" << std::hex << magic << std::dec << std::endl;
        std::cout << "mapping_store     : " << mapping_store << std::endl;
        std::cout << "mapping_page_num  : " << mapping_page_num << std::endl;
        std::cout << "log_store         : " << log_store << std::endl;
        std::cout << "log_page_num      : " << log_page_num << std::endl;
        std::cout << "currentLogLBN     : " << currentLogLBN << std::endl;
        std::cout << "nextLogLBN        : " << nextLogLBN << std::endl;
        std::cout << "logOffset         : " << logOffset << std::endl;
        std::cout << "byteOffset        : " << byteOffset << std::endl;
        std::cout << "firstBlockOffset  : " << firstBlockOffset << std::endl;
        std::cout << "usedLBN_num       : " << usedLBN_num << std::endl;
        std::cout << "lastUsedChannel   : " << static_cast<int>(lastUsedChannel) << std::endl;
        std::cout << "global_sequence   : " << global_sequence << std::endl;
        std::cout << "sstable_sequence  : " << sstable_sequence << std::endl;
        std::cout << "===================================" << std::endl;
    }

};
static_assert(sizeof(super_page) == IMS_PAGE_SIZE, "super_page must be same to page size");
#pragma pack(pop)
// [IMS setting end]


// [mapping table setting start]
#pragma pack(push, 1)
struct mappingEntry {
    uint64_t lbn;
    uint8_t level;
    uint8_t channel;
    Key minRange;
    Key maxRange;
    static constexpr size_t fileNameSize =
        128 - sizeof(uint64_t)  // lbn
           - sizeof(uint8_t)   // level
           - sizeof(uint8_t)   // channel
           - sizeof(Key)  // minRange
           - sizeof(Key); // maxRange
    char fileName[fileNameSize];

    mappingEntry() : lbn(0xFFFFFFFFFFFFFFFF) {
        memset(fileName, 0, sizeof(fileName));
    }
};

static_assert(sizeof(mappingEntry) == 128, "mappingEntry must be 128 bytes");

#define MAPPING_TABLE_ENTRIES ( (IMS_PAGE_SIZE / sizeof(mappingEntry))-1 ) // 16384 / 64(mapping entry) = 128 , 128 - 1(header) = 127

struct mappingTablePerPage {
    
    union {
        uint8_t header[sizeof(mappingEntry)];
        struct {
            // Record the next page is stored mappingTable
            uint8_t entry_num;
            uint8_t reserved1[63]; 
        };
    };
    
    mappingEntry entry[MAPPING_TABLE_ENTRIES]; 
    uint8_t reserved2[IMS_PAGE_SIZE - (MAPPING_TABLE_ENTRIES * sizeof(mappingEntry)) - sizeof(header)]; // 填滿16KB

    mappingTablePerPage() {
        memset(this, 0xFF, sizeof(mappingTablePerPage));
    }
};
#pragma pack(pop)
static_assert(sizeof(mappingTablePerPage) == 16384, "MappingTablePage must be 16KB");

// [mapping table setting end]



// [SStable setting start]

#pragma pack(push, 1)

struct slotFormat {
    uint8_t key_size;           // 1 B
    uint8_t key[40];            // 40 B

    struct {
        union {
            uint32_t lpn;
            struct {
                uint32_t ch      : 3;
                uint32_t plane   : 3;
                uint32_t die     : 3;
                uint32_t package : 3;
                uint32_t block   : 10;
                uint32_t page    : 10;
            };
        };
        uint32_t offset;
        uint8_t reserve[7];
    } value_ptr;                // 15 B

    union {
        uint8_t raw[8]; 
        struct {
            uint64_t seq  : 56;
            uint64_t type : 8;
        };
    } info;                     // = 8 B
};
static_assert(sizeof(slotFormat) == 64, "slotFormat must be 64 bytes");


#define SLOT_SIZE sizeof    (slotFormat)
#define SLOT_NUM_PER_PAGE   (IMS_PAGE_SIZE/sizeof(slotFormat))
#define SLOT_NUM_PER_BLOCK  (SLOT_NUM_PER_PAGE * IMS_PAGE_NUM)
struct pageFormat
{
    slotFormat slot[SLOT_NUM_PER_PAGE];
};
static_assert(sizeof(pageFormat) == IMS_PAGE_SIZE ,"pageformat must be same to page size");

struct  SStableFormat
{
    pageFormat SStablePerPage[IMS_PAGE_NUM];
};
static_assert(sizeof(SStableFormat) == BLOCK_SIZE ,"SStableFormat must be same to block size");
#pragma pack(pop)

// [SStable setting end]



// [Log file setting]

#define LOG_FILE_LBN 1

#pragma pack(push, 1)
struct logLBNListRecord {
    uint32_t lbn[IMS_PAGE_SIZE / sizeof(uint32_t)]; // The LBN of the log record
};

static_assert(sizeof(logLBNListRecord) == IMS_PAGE_SIZE, "logLBNListRecord must be same to page size");

struct logRecord{
    uint32_t key_size;
    uint32_t value_size;
    uint8_t key[40];
    uint8_t *value;
};
#pragma pack(pop)
// [Log file setting end]



struct  DB_INIT {
    // struct{
    //     uint32_t magic;
    //     uint32_t payload_size;
    // }header;
    uint64_t sstable_seq;
    uint64_t global_seq;
    uint32_t next_lbn;
    uint32_t current_lbn;
    uint32_t page_offset;
    uint32_t byte_offset;
    uint32_t first_block_offset;
    std::string log_list;
    std::string node_list;
    DB_INIT() : 
        sstable_seq(0), 
        global_seq(0), 
        next_lbn(0), 
        current_lbn(0), 
        page_offset(0),
        byte_offset(0),
        first_block_offset(0){}
    void dump() const; 
    std::string encode();
    static bool decode(const std::string& buf,DB_INIT& out);
};

// [search key setting]



struct SearchPatternD{
    std::string sstable_name;  //36B
    uint32_t slot_index;
    std::string encode() const;
    static bool decode(const std::string& buf, SearchPatternD& out);
    void dump() const; 
};

struct SearchPackageD{
    struct {
        uint32_t magic;
        uint32_t pattern_num;
    } header;
    std::string search_key;
    std::vector<SearchPatternD> searchPatterns;
    std::string encode() const;
    static bool decode(const std::string& buf, SearchPackageD& out);
    void dump() const;
};

struct SearchPatternH{
    std::string sstable_name;  // 36B
    std::string search_pattern; // 16KB 
    std::string encode() const;
    static bool decode(const std::string& buf, SearchPatternH& out);
    void dump() const; 
};

struct SearchPackageH{
    struct {
        uint32_t magic;
        uint32_t pattern_num;
    } header;
    std::string search_key;
    std::vector<SearchPatternH> searchPatterns;
    std::string encode() const;
    static bool decode(const std::string& buf, SearchPackageH& out);
    void dump() const;
};



#define READ_CACHE_CAPACITY 20


// [search_key setting end]
#endif // __DEF_HH__