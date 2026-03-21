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
    kTypeMin        = 0x0,
    kTypeDeletion   = 0x1,
    kTypeValue      = 0x2,
    kTypeMax        = 0x3,
    kInvalid        = 0xFF
};

struct Key {
    static constexpr size_t MAX_KEY_BYTES = 40;
    static constexpr size_t ENCODED_SIZE  = 1 + MAX_KEY_BYTES; // key_size(1) + data(40)

    uint8_t key_size;                    // 實際有效長度 (0 ~ 40)
    uint8_t key[MAX_KEY_BYTES];          // 固定 40 bytes buffer

    Key() : key_size(0) {
        std::memset(key, 0, sizeof(key));
    }

    explicit Key(const std::string& str) : key_size(0) {
        std::memset(key, 0, sizeof(key));
        fromString(str);
    }

    void fromString(const std::string& str) {
        size_t sz = std::min(MAX_KEY_BYTES, str.size());
        key_size  = static_cast<uint8_t>(sz);

        // 先清 0 再寫，避免殘留舊資料
        std::memset(key, 0, sizeof(key));
        if (sz > 0) {
            std::memcpy(key, str.data(), sz);
        }
    }

    std::string toString() const {
        return std::string(reinterpret_cast<const char*>(key),
                           static_cast<size_t>(key_size));
    }

    void dumpUint() const {
        std::cout << "Key (hex): ";
        for (size_t i = 0; i < key_size; ++i) {
            std::cout << "0x" << std::hex << std::setw(2)
                      << std::setfill('0') << +key[i] << " ";
        }
        std::cout << std::dec << "\n";
    }

    void dumpString() const {
        std::cout << "Key: " << toString()
                  << " (size: " << +key_size << ")\n";
    }

    // 固定輸出 41 bytes：1 byte key_size + 40 bytes data
    std::string encode() const {
        std::string out;
        out.reserve(ENCODED_SIZE);

        out.push_back(static_cast<char>(key_size));
        out.append(reinterpret_cast<const char*>(key), MAX_KEY_BYTES);

        return out;
    }

    // 安全版 decode：檢查長度
    static bool decode(const char* data, size_t len, Key& out) {
        if (!data || len < ENCODED_SIZE) {
            return false;
        }

        uint8_t sz_raw = static_cast<uint8_t>(data[0]);
        uint8_t sz = (sz_raw <= MAX_KEY_BYTES)
                     ? sz_raw
                     : static_cast<uint8_t>(MAX_KEY_BYTES);

        out.key_size = sz;
        std::memcpy(out.key, data + 1, MAX_KEY_BYTES);

        return true;
    }

    // 舊介面：假設呼叫方保證 buffer 至少 41 bytes
    static Key decode(const char* data) {
        Key k;
        (void)decode(data, ENCODED_SIZE, k);
        return k;
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
            uint64_t type : 8;
            uint64_t seq  : 56;
        };
    } info;

    bool operator()(const InternalKey& a,const InternalKey& b) const;
    InternalKey();
    // InternalKey(const std::string_view);
    InternalKey(const char* user_key,size_t size);
    InternalKey(const std::string& user_key);
    InternalKey(const std::string& user_key, uint64_t seq, ValueType t);
    InternalKey(const std::string& user_key, uint32_t lpn,uint32_t offset,uint64_t seq, ValueType t);
    static InternalKey Decode(const char* buf);
    void EncodeTo(char* dst) const;
    bool IsValid() const;
    std::string Encode() const;
    static InternalKey Decode(const std::string& buf);
    static InternalKey Decode(char* buf);
    std::string UserKey() const;
    void dump() const;

};
#pragma pack(pop)
static_assert(sizeof(InternalKey) == 64, "InternalKey must be 64 bytes");
static_assert(sizeof(InternalKey::value_ptr) == 15,
              "value_ptr must be 15 bytes; verify every TU has pragma pack(1)");
struct InternalKeyComparator {
    bool operator()(const InternalKey& a, const InternalKey& b) const;
};

struct SetComparator {
    bool operator()(const InternalKey& a, const InternalKey& b) const;
};


#endif  // INTERNAL_KEY_HH
