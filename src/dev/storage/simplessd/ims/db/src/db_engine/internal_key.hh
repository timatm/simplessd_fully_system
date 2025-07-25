#ifndef INTERNAL_KEY_HH
#define INTERNAL_KEY_HH

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <cassert>
#include <iostream>
#include <iomanip>
// 8-bit type enum
enum class ValueType : uint8_t {
    kTypeDeletion = 0x0,
    kTypeValue = 0x1,
};
struct Key {
    uint8_t key_size;
    uint8_t key[40];

    Key() : key_size(0) {
        std::memset(key, 0, sizeof(key));
    }

    void fromString(const std::string& str) {
        key_size = std::min(static_cast<size_t>(40), str.size());
        std::memcpy(key, str.data(), key_size);
    }

    std::string toString() const {
        return std::string(reinterpret_cast<const char*>(key), key_size);
    }

    void dumpUint() const {
        std::cout << "Key (hex): ";
        for (int i = 0; i < key_size; ++i)
            std::cout << "0x" << std::hex << std::setw(2)
                      << std::setfill('0') << +key[i] << " ";
        std::cout << std::dec << "\n";
    }

    void dumpString() const {
        std::cout << "Key: " << toString()
                  << " (size: " << +key_size << ")" << std::endl;
    }
};

#pragma pack(push,1)
struct InternalKey {
    Key key; // 41 bytes
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
    InternalKey(const std::string& user_key, uint32_t lpn,uint32_t offset,uint64_t seq, ValueType t);
    std::string Encode() const;
    static InternalKey Decode(const std::string& buf);
    std::string UserKey() const;
    void dump() const;

};
#pragma pack(pop)
static_assert(sizeof(InternalKey) == 64, "InternalKey must be 64 bytes");

struct InternalKeyComparator {
    bool operator()(const InternalKey& a, const InternalKey& b) const;
};

#endif  // INTERNAL_KEY_HH
