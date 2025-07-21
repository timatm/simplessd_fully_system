#ifndef INTERNAL_KEY_HH
#define INTERNAL_KEY_HH

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <cassert>

// 8-bit type enum
enum class ValueType : uint8_t {
    kTypeDeletion = 0x0,
    kTypeValue = 0x1,
};

struct InternalKey {
    uint8_t key_size;       // 1B
    uint8_t key[40];        // 最多 40B，實際用前 key_size B

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
    } value_ptr;

    union {
        uint8_t raw[8];
        struct {
            uint64_t seq  : 56;
            uint64_t type : 8;
        };
    } info;

    InternalKey();
    InternalKey(const std::string& user_key, uint64_t seq, ValueType t);

    std::string Encode() const;
    static InternalKey Decode(const std::string& buf);
    std::string UserKey() const;
};

struct InternalKeyComparator {
    bool operator()(const InternalKey& a, const InternalKey& b) const;
};

#endif  // INTERNAL_KEY_HH
