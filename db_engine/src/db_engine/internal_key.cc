#include "internal_key.hh"
#include <iostream>
#include <iomanip>
#include "print.hh"

// Default constructor: zero everything
InternalKey::InternalKey() {
    std::memset(this, 0, sizeof(InternalKey));
}

// InternalKey::InternalKey(const std::string_view user_key){
//     assert(user_key.size() <= 40);
//     key.key_size = static_cast<uint8_t>(user_key.size());
//     std::memcpy(key.key, user_key.data(), key.key_size);
//     std::memset(key.key + key.key_size, 0, 40 - key.key_size);
// }

InternalKey::InternalKey(const char* user_key,size_t size){
    assert(size <= 40);
    key.key_size = static_cast<uint8_t>(size);
    std::memcpy(key.key, user_key, key.key_size);
    std::memset(key.key + key.key_size, 0, 40 - key.key_size);
}


InternalKey::InternalKey(const std::string& user_key){
    assert(user_key.size() <= 40);
    key.key_size = static_cast<uint8_t>(user_key.size());
    std::memcpy(key.key, user_key.data(), key.key_size);
    std::memset(key.key + key.key_size, 0, 40 - key.key_size);
}

// UserKey + seq/type constructor
InternalKey::InternalKey(const std::string& user_key, uint64_t seq, ValueType t) {
    assert(user_key.size() <= 40);
    key.key_size = static_cast<uint8_t>(user_key.size());
    std::memcpy(key.key, user_key.data(), key.key_size);
    std::memset(key.key + key.key_size, 0, 40 - key.key_size);  // zero padding
    std::memset(&value_ptr, 0, sizeof(value_ptr));
    info.seq = seq;
    info.type = static_cast<uint8_t>(t);
}

// UserKey + lpn/offset/seq/type constructor
InternalKey::InternalKey(const std::string& user_key, uint32_t lpn, uint32_t offset, uint64_t seq, ValueType t) {
    assert(user_key.size() <= 40);
    key.key_size = static_cast<uint8_t>(user_key.size());
    std::memcpy(key.key, user_key.data(), key.key_size);
    std::memset(key.key + key.key_size, 0, 40 - key.key_size);  // zero padding
    value_ptr.lpn = lpn;
    value_ptr.offset = offset;
    std::memset(value_ptr.reserve, 0, sizeof(value_ptr.reserve));
    info.seq = seq;
    info.type = static_cast<uint8_t>(t);
}

// Encode to binary string
std::string InternalKey::Encode() const {
    std::string out;
    out.push_back(static_cast<char>(key.key_size));
    out.append(reinterpret_cast<const char*>(key.key), 40);

    // —— value_ptr：手動寫 15 bytes，保證不受 pack 影響 ——
    out.append(reinterpret_cast<const char*>(&value_ptr.lpn),     4);
    out.append(reinterpret_cast<const char*>(&value_ptr.offset),  4);
    out.append(reinterpret_cast<const char*>(value_ptr.reserve),  7);

    // —— meta：明確 cast ——  
    uint64_t meta = (static_cast<uint64_t>(info.seq) << 8) |
                    static_cast<uint8_t>(info.type);
    out.append(reinterpret_cast<const char*>(&meta), sizeof(meta));

    return out;
}

InternalKey InternalKey::Decode(const std::string& buf) {
    InternalKey ik{};
    if (buf.size() != sizeof(InternalKey)) {
        pr_debug("InternalKey size is invalid(%d)",buf.size());
        return ik;
    }
    size_t off = 0;

    ik.key.key_size = static_cast<uint8_t>(buf[off++]);
    std::memcpy(ik.key.key, buf.data() + off, 40); off += 40;

    std::memcpy(&ik.value_ptr.lpn,    buf.data() + off, 4); off += 4;
    std::memcpy(&ik.value_ptr.offset, buf.data() + off, 4); off += 4;
    std::memcpy( ik.value_ptr.reserve,buf.data() + off, 7); off += 7;

    uint64_t meta;
    std::memcpy(&meta, buf.data() + off, sizeof(meta));

    ik.info.seq  = meta >> 8;
    ik.info.type = static_cast<uint8_t>(meta & 0xFF);

    return ik;
}

// bool InternalKey::Decode(std::string_view bytes) {
//     // 先解析 user key 部分……
//     // 然后读出 8B tag（LE）：
//     uint64_t tag = DecodeFixed64(ptr);  // 按小端
//     info.type = static_cast<uint8_t>(tag & 0xFF);
//     info.seq  = tag >> 8;
//     return true;
// }

InternalKey InternalKey::Decode(char* buf) {
    InternalKey ik{};
    
    size_t off = 0;

    ik.key.key_size = static_cast<uint8_t>(buf[off++]);
    std::memcpy(ik.key.key, buf + off, 40); off += 40;

    std::memcpy(&ik.value_ptr.lpn,    buf + off, 4); off += 4;
    std::memcpy(&ik.value_ptr.offset, buf + off, 4); off += 4;
    std::memcpy( ik.value_ptr.reserve,buf + off, 7); off += 7;

    uint64_t meta;
    std::memcpy(&meta, buf + off, sizeof(meta));

    ik.info.seq  = meta >> 8;
    ik.info.type = static_cast<uint8_t>(meta & 0xFF);

    return ik;
}


// Extract user key string
std::string InternalKey::UserKey() const {
    return std::string(reinterpret_cast<const char*>(key.key), key.key_size);
}

// Comparator: sort by key, then seq(desc), then type(desc)
bool InternalKeyComparator::operator()(const InternalKey& a, const InternalKey& b) const {
    int cmp = std::memcmp(a.key.key, b.key.key, std::min(a.key.key_size, b.key.key_size));
    if (cmp == 0) {
        if (a.key.key_size != b.key.key_size)
            return a.key.key_size < b.key.key_size;
        if (a.info.seq != b.info.seq)
            return a.info.seq > b.info.seq;  // higher seq first
        return a.info.type < b.info.type;    // tombstone first
    }
    return cmp < 0;
}

bool SetComparator::operator()(const InternalKey& a, const InternalKey& b) const {
    int cmp = std::memcmp(a.key.key, b.key.key, std::min(a.key.key_size, b.key.key_size));
    if (cmp == 0) {
        if (a.key.key_size != b.key.key_size)
            return a.key.key_size < b.key.key_size;
    }
    return cmp < 0;
}


void InternalKey::dump() const {
    std::cout << "=== InternalKey Dump ===\n";
    std::cout << "UserKey   : " << key.toString() << "\n";
    std::cout << "Key Size  : " << +key.key_size << "\n";
    
    std::cout << "ValuePtr  :\n";
    std::cout << "  LPN     : " << value_ptr.lpn << "\n";
    std::cout << "  Offset  : " << value_ptr.offset << "\n";

    std::cout << "Info      :\n";
    std::cout << "  Seq     : " << info.seq << "\n";
    std::cout << "  Type    : " << static_cast<int>(info.type) << " ("
              << (info.type == static_cast<uint8_t>(ValueType::kTypeValue) ? "Put" : "Deletion") << ")\n";

    std::cout << "=========================\n";
}

bool InternalKey::operator()(const InternalKey& a, const InternalKey& b) const {
    int cmp = std::memcmp(a.key.key, b.key.key, std::min(a.key.key_size, b.key.key_size));
    if (cmp != 0) return cmp < 0;
    if (a.key.key_size != b.key.key_size) return a.key.key_size < b.key.key_size;
    if (a.info.seq != b.info.seq) return a.info.seq > b.info.seq;  
    return a.info.type > b.info.type;
}                      
// 建議改成 const 成員函式
bool InternalKey::IsValid() const {
    // 基本形狀檢查
    if (key.key_size > 40) return false;

    // 型別必須在我們定義的合法範圍內（0..3），且不能是 kInvalid(0xFF)
    const uint8_t t = static_cast<uint8_t>(info.type);
    if  (t == static_cast<uint8_t>(ValueType::kInvalid)) return false;
    if  (t >= static_cast<uint8_t>(ValueType::kTypeMax)) return false;
    if  (t <= static_cast<uint8_t>(ValueType::kTypeMin)) return false;

    return true;
}

