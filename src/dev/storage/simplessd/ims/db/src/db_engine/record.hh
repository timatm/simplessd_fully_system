#ifndef RECORD_HH
#define RECORD_HH

#include <string>
#include "internal_key.hh"

#pragma pack(push, 1) 
struct Record {
    uint32_t internal_key_size;
    uint32_t value_size;
    InternalKey internal_key;
    std::string value;

    Record() = default;
    Record(const InternalKey& ikey, const std::string& val);

    std::string Encode() const;
    static Record Decode(const std::string& data);
    void Dump() const;
};

#pragma pack(pop)

#endif  // RECORD_HH
