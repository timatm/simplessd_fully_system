#ifndef __OPTIONS__HH__
#define __OPTIONS__HH__
enum class PackingType {
    kKeyPerPage     = 0x0,
    kHash           = 0x1,
    kKeyRange       = 0x2,
};

enum PutType{
    kPutByUser,
    kPutByGC
};


#define PACKING_T (PackingType::kKeyPerPage)

#define LEVEL0_MAX 4
#define LEVEL1_MAX 10
#define LEVEL2_MAX LEVEL1_MAX * 10
#define LEVEL3_MAX LEVEL2_MAX * 10
#define LEVEL4_MAX LEVEL3_MAX * 10
#define LEVEL5_MAX LEVEL4_MAX * 10
#define LEVEL6_MAX LEVEL5_MAX * 10


#define RANGE_KEY_CACHE_SIZE 30


// Search pattern generate in HOST / DEVICE
// 0: DEVICE
// 1: HOST
#define SEARCH_PATTERN 0

#define LOG_GC_THRESHOLD 1000
#define GC_BLOCK_NUM 1

#endif  // __OPTIONS__HH__