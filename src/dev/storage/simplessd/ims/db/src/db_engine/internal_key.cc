#include "internal_key.hh"
#include <iostream>
#include <iomanip>

// Default constructor: zero everything
InternalKey::InternalKey() {
    std::memset(this, 0, sizeof(InternalKey));
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
    out.append(reinterpret_cast<const char*>(&value_ptr), sizeof(value_ptr));
    out.append(reinterpret_cast<const char*>(info.raw), sizeof(info.raw));
    return out;
}

// Decode from binary string
InternalKey InternalKey::Decode(const std::string& buf) {
    assert(buf.size() == sizeof(InternalKey));  // precise check
    InternalKey ik;
    size_t offset = 0;

    ik.key.key_size = static_cast<uint8_t>(buf[offset++]);
    std::memcpy(ik.key.key, buf.data() + offset, 40); offset += 40;
    std::memcpy(&ik.value_ptr, buf.data() + offset, sizeof(ik.value_ptr)); offset += sizeof(ik.value_ptr);
    std::memcpy(ik.info.raw, buf.data() + offset, sizeof(ik.info.raw));
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
        return a.info.type > b.info.type;    // higher type wins
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
