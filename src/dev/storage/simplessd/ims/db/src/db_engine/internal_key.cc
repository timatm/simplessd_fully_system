#include "internal_key.hh"

InternalKey::InternalKey() {
    std::memset(this, 0, sizeof(InternalKey));
}

InternalKey::InternalKey(const std::string& user_key, uint64_t seq, ValueType t) {
    assert(user_key.size() <= 40);
    key_size = static_cast<uint8_t>(user_key.size());
    std::memcpy(key, user_key.data(), key_size);
    std::memset(key + key_size, 0, 40 - key_size);  // zero padding
    std::memset(&value_ptr, 0, sizeof(value_ptr));
    info.seq = seq;
    info.type = static_cast<uint8_t>(t);
}

InternalKey::InternalKey(const std::string& user_key, uint32_t lpn,uint32_t offset,int64_t seq, ValueType t) {
    assert(user_key.size() <= 40);
    key_size = static_cast<uint8_t>(user_key.size());
    std::memcpy(key, user_key.data(), key_size);
    std::memset(key + key_size, 0, 40 - key_size);  // zero padding
    std::memset(&value_ptr, 0, sizeof(value_ptr));
    value_ptr.lpn = lpn;
    value_ptr.offset = offset;
    info.seq = seq;
    info.type = static_cast<uint8_t>(t);
}

std::string InternalKey::Encode() const {
    std::string out;
    out.push_back(static_cast<char>(key_size));
    out.append(reinterpret_cast<const char*>(key), 40);
    out.append(reinterpret_cast<const char*>(&value_ptr), sizeof(value_ptr));
    out.append(reinterpret_cast<const char*>(info.raw), 8);
    return out;
}

InternalKey InternalKey::Decode(const std::string& buf) {
    assert(buf.size() > 1 + 40 + 15 + 8);
    InternalKey ik;
    size_t offset = 0;

    ik.key_size = static_cast<uint8_t>(buf[offset++]);
    std::memcpy(ik.key, buf.data() + offset, 40); offset += 40;
    std::memcpy(&ik.value_ptr, buf.data() + offset, sizeof(ik.value_ptr)); offset += sizeof(ik.value_ptr);
    std::memcpy(ik.info.raw, buf.data() + offset, 8);

    return ik;
}

std::string InternalKey::UserKey() const {
    return std::string(reinterpret_cast<const char*>(key), key_size);
}

bool InternalKeyComparator::operator()(const InternalKey& a, const InternalKey& b) const {
    int cmp = std::memcmp(a.key, b.key, std::min(a.key_size, b.key_size));
    if (cmp == 0) {
        if (a.key_size != b.key_size)
            return a.key_size < b.key_size;
        if (a.info.seq != b.info.seq)
            return a.info.seq > b.info.seq;
        return a.info.type > b.info.type;
    }
    return cmp < 0;
}
