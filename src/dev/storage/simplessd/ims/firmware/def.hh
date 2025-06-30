#ifndef __DEF_HH__
#define __DEF_HH__

#include <cstdint>
#include <cstring>
// [SSD setting start]

#define CHANNEL_NUM 4
#define PACKAGE_NUM 4
#define DIE_NUM 2
#define PLANE_NUM 2
#define BLOCK_NUM 32
#define IMS_PAGE_NUM 128
#define IMS_PAGE_SIZE 16384

#define BLOCK_SIZE IMS_PAGE_SIZE*IMS_PAGE_NUM

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

#define LBN_NUM ( CHANNEL_NUM * PACKAGE_NUM * DIE_NUM * PLANE_NUM * BLOCK_NUM )
#define LBN_SIZE ( PAGE_SIZE * IMS_PAGE_NUM )

#define LBN2PLANE(LBA)  ( (LBA >> BLOCK_BITS) % PLANE_NUM )
#define LBN2DIE(LBA)    ( (LBA >> (PLANE_BITS + BLOCK_BITS)) % DIE_NUM )
#define LBN2PACKAGE(LBA)( (LBA >> (DIE_BITS + PLANE_BITS + BLOCK_BITS)) % PACKAGE_NUM )
#define LBN2CH(LBA)     ( (LBA >> (PACKAGE_BITS + DIE_BITS + PLANE_BITS + BLOCK_BITS)) % CHANNEL_NUM )

#define LBN2LPN(lbn) (lbn * IMS_PAGE_NUM) 

#define OPERATION_SUCCESS 0
#define OPERATION_FAILURE -1

#define ENABLE_DISK 1

#define INVALIDLBN 0xFFFFFFFFFFFFFFFF
#define INVALIDCH  0xFF
// [SSD setting end]


// [IMS setting start]
#define SUPER_BLOCK 0
#define HAS_NEXT_PAGE 1
#define NO_NEXT_PAGE  0


#define DISPATCH_POLICY 3 // 0: worst case, 1: RR, 2: level2CH, 3: my_policy
#define MAX_LEVEL 7
#define MAX_FILENAME_LENGTH 56 // SStable file name length
#define MAGIC 0x900118FFFEEFFFEE

struct hostInfo
{
    uint64_t lbn;
    std::string filename;
    int levelInfo;
    int channelInfo;
    int rangeMin;
    int rangeMax;
    hostInfo(std::string name, int level, int ch, int min, int max) :
        filename(std::move(name)),
        levelInfo(level),
        channelInfo(ch),
        rangeMin(min),
        rangeMax(max) {}
    hostInfo(std::string name, int level, int min, int max) :
        hostInfo(std::move(name), level, -1, min, max) {}
};

struct valueLogInfo{
    uint64_t lbn;  // This log store in which LBN
    uint64_t page_offset; // This log store in which LBN's LPN
    uint8_t *buffer; //data buffer
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
    uint64_t usedLBN_num;
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
        sizeof(usedLBN_num) +
        sizeof(lastUsedChannel);
    uint8_t reserved[IMS_PAGE_SIZE - header_size];
    super_page(uint64_t m,uint64_t mapping_store,uint64_t log_store):
        magic(m),
        mapping_store(mapping_store),
        mapping_page_num(0),
        log_store(log_store),
        log_page_num(0),
        currentLogLBN(INVALIDLBN),
        nextLogLBN(INVALIDLBN),
        logOffset(INVALIDLBN),
        usedLBN_num(INVALIDLBN),
        lastUsedChannel(INVALIDCH){}
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
    uint32_t minRange;
    uint32_t maxRange;
    char fileName[64-sizeof(lbn) - sizeof(level) - sizeof(channel) - sizeof(minRange) - sizeof(maxRange)]; // 64 bytes total
    mappingEntry() : lbn(0xFFFFFFFFFFFFFFFF) {
        memset(fileName, 0, sizeof(fileName));
    }
};
#define MAPPING_TABLE_ENTRIES ( (IMS_PAGE_SIZE / sizeof(mappingEntry))-1 ) // 16384 / 64(mapping entry) = 128 , 128 - 1(header) = 127

struct mappingTablePerPage {
    
    union {
        uint8_t header[sizeof(mappingEntry)];
         // 完整 raw data
        struct {
            // Record the next page is stored mappingTable
            uint8_t entry_num;     // 1 bytes
            uint8_t reserved1[62];   // 61 bytes
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
            uint32_t cpdpbp;    // 4 B = packed bit-field
            struct {
                uint32_t ch      : 3;
                uint32_t plane   : 3;
                uint32_t die     : 3;
                uint32_t package : 3;
                uint32_t block   : 10;
                uint32_t page    : 10;
            };
        };
        uint8_t offset[11];     // 11 B
    } value_ptr;                // = 15 B

    union {
        uint8_t raw[8];         // raw access
        struct {
            uint64_t seq  : 56; // 7 B
            uint64_t type : 8;  // 1 B
        };
    } info;                     // = 8 B
};

#define SLOT_NUM IMS_PAGE_SIZE/sizeof(slotFormat)
struct pageFormat
{
    slotFormat slot[SLOT_NUM];
};
struct  SStableFormat
{
    pageFormat SStablePerPage[IMS_PAGE_NUM];
};


static_assert(sizeof(slotFormat) == 64, "slotFormat must be 64 bytes");
static_assert(sizeof(pageFormat) == IMS_PAGE_SIZE ,"pageformat must be same to page size");
static_assert(sizeof(SStableFormat) == BLOCK_SIZE ,"SStableFormat must be same to block size");
#pragma pack(pop)

// [SStable setting end]



// [Log file setting]

#define LOG_FILE_LBN 1

#pragma pack(push, 1)
struct logLBNListRecord {
    uint64_t lbn[IMS_PAGE_SIZE / sizeof(uint64_t)]; // The LBN of the log record
};
static_assert(sizeof(logLBNListRecord) == IMS_PAGE_SIZE, "logLBNListRecord must be same to page size");
#pragma pack(pop)
// [Log file setting end]


#endif // __DEF_HH__