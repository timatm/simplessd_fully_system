#ifndef RECORD_HH
#define RECORD_HH

#include <string>
#include "internal_key.hh"

struct Record {
    uint16_t internal_key_size;
    InternalKey internal_key;

    uint32_t value_size;
    std::string value;

    Record() = default;
    Record(const InternalKey& ikey, const std::string& val);

    std::string Encode() const;
    static Record Decode(const std::string& data);
    void Dump() const;
};

#endif  // RECORD_HH
