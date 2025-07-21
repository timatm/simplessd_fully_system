#include "memtable.hh"
#include <iostream>
#include <limits>

void MemTable::Put(const std::string& user_key, const std::string& value, uint64_t seq) {
    InternalKey ikey(user_key, seq, ValueType::kTypeValue);
    skiplist_.Insert({ikey, value});
}

void MemTable::Delete(const std::string& user_key, uint64_t seq) {
    InternalKey ikey(user_key, seq, ValueType::kTypeDeletion);
    skiplist_.Insert({ikey, ""});
}

std::optional<std::string> MemTable::Get(const std::string& user_key) const {
    InternalKey lookup(user_key, std::numeric_limits<uint64_t>::max(), ValueType::kTypeValue);
    auto iter = skiplist_.GetIterator();
    iter.Seek({lookup, ""});
    if (iter.Valid() && iter.key().first.user_key == user_key) {
        if (iter.key().first.type == ValueType::kTypeDeletion) return std::nullopt;
        return iter.key().second;
    }
    return std::nullopt;
}

void MemTable::Dump() const {
    auto iter = skiplist_.GetIterator();
    iter.SeekToFirst();
    while (iter.Valid()) {
        const auto& [k, v] = iter.key();
        std::cout << k.user_key << " [seq=" << k.sequence << "] => ";
        if (k.type == ValueType::kTypeDeletion) std::cout << "<DELETED>";
        else std::cout << v;
        std::cout << '\n';
        iter.Next();
    }
}
