#ifndef __OPTIONS__HH__
#define __OPTIONS__HH__
enum class PackingType {
    kKeyPerPage     = 0x0,
    kHash           = 0x1,
    kKeyRange       = 0x2,
    kIdxBloomData   = 0x3,
};

enum PutType{
    kPutByUser,
    kPutByGC
};


#ifndef PACKING_TYPE
#define PACKING_TYPE 0
#endif

constexpr PackingType kPackingType = static_cast<PackingType>(PACKING_TYPE);

static_assert(
    PACKING_TYPE == 0 || PACKING_TYPE == 1 || PACKING_TYPE == 2 || PACKING_TYPE == 3,
    "PACKING_TYPE must be 0(kKeyPerPage), 1(kHash), 2(kKeyRange), or 3(kIdxBloomData)"
);



#ifndef IDX_BLOOM_META_PAGES
#define IDX_BLOOM_META_PAGES 2
#endif

#ifndef IDX_BLOOM_HASH_K
#define IDX_BLOOM_HASH_K 5
#endif


// cache    200MB = 12800
//          500MB = 32000
#define RANGE_KEY_CACHE_SIZE 32000


// // Search pattern generate in HOST / DEVICE
// // 0: DEVICE
// // 1: HOST
// #define SEARCH_PATTERN 1

#define LOG_GC_THRESHOLD 100000
#define GC_BLOCK_NUM 1

#endif  // __OPTIONS__HH__