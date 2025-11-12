#include "record.hh"
#include <cassert>
#include <cstring>
#include <iostream>
Record::Record(const InternalKey& ikey, const std::string& val)
    : internal_key_size(sizeof(ikey)),
      internal_key(ikey),
      value_size(static_cast<uint32_t>(val.size())),
      value(val) {}

Record::Record(const InternalKey& ikey)
    : internal_key_size(sizeof(ikey)),
      internal_key(ikey),
      value_size(0),
      value() {}


std::string Record::Encode() const {
    std::string buf;
    buf.reserve(sizeof(internal_key_size) + sizeof(value_size) +
                internal_key_size + value_size);
    buf.append(reinterpret_cast<const char*>(&internal_key_size),
               sizeof(internal_key_size));
    buf.append(reinterpret_cast<const char*>(&value_size),
               sizeof(value_size));

    buf.append(internal_key.Encode());
    buf.append(value);

    return buf;
}


Record Record::Decode(const std::string& data) {
    size_t offset = 0;
    Record rec;

    // 1) key size
    std::memcpy(&rec.internal_key_size, data.data() + offset,
                sizeof(rec.internal_key_size));
    offset += sizeof(rec.internal_key_size);

    // 2) value size
    std::memcpy(&rec.value_size, data.data() + offset,
                sizeof(rec.value_size));
    offset += sizeof(rec.value_size);

    // 3) key
    std::string ikey_blob = data.substr(offset, rec.internal_key_size);
    rec.internal_key = InternalKey::Decode(ikey_blob);
    offset += rec.internal_key_size;

    // 4) value
    rec.value = data.substr(offset, rec.value_size);

    return rec;
}


void Record::Dump() const {
    std::cout << "== Record Dump ==" << std::endl;
    std::cout << "User Key: " << internal_key.UserKey() << std::endl;
    std::cout << "Sequence : " << internal_key.info.seq << std::endl;
    std::cout << "Type     : " << static_cast<uint32_t>(internal_key.info.type) << std::endl;
    std::cout << "LPN      : " << internal_key.value_ptr.lpn << std::endl;
    std::cout << "Offset   : " << internal_key.value_ptr.offset << std::endl;
    std::cout << "Value Size : " << value_size << std::endl;
    std::cout << "Value    : " << value << std::endl;
    std::cout << "====================" << std::endl;
}