#include "record.hh"
#include <cassert>
#include <cstring>

Record::Record(const InternalKey& ikey, const std::string& val)
    : internal_key_size(sizeof(ikey)),
      internal_key(ikey),
      value_size(static_cast<uint32_t>(val.size())),
      value(val) {}

std::string Record::Encode() const {
    std::string result;

    // internal_key_size (2 bytes)
    result.append(reinterpret_cast<const char*>(&internal_key_size), sizeof(internal_key_size));

    // internal_key (struct binary layout)
    result.append(internal_key.Encode());

    // value_size (4 bytes)
    result.append(reinterpret_cast<const char*>(&value_size), sizeof(value_size));

    // value string
    result.append(value);

    return result;
}

Record Record::Decode(const std::string& data) {
    size_t offset = 0;
    Record rec;

    // 1. internal_key_size
    std::memcpy(&rec.internal_key_size, data.data() + offset, sizeof(rec.internal_key_size));
    offset += sizeof(rec.internal_key_size);

    // 2. internal_key
    std::string ikey_blob = data.substr(offset, rec.internal_key_size);
    rec.internal_key = InternalKey::Decode(ikey_blob);
    offset += rec.internal_key_size;

    // 3. value_size
    std::memcpy(&rec.value_size, data.data() + offset, sizeof(rec.value_size));
    offset += sizeof(rec.value_size);

    // 4. value
    rec.value = data.substr(offset, rec.value_size);

    return rec;
}
