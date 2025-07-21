#ifndef MEMTABLE_HH_
#define MEMTABLE_HH_

#include <string>
#include <optional>
#include "skiplist.hh"
#include "internal_key.hh"

struct InternalKeyPairComparator {
    bool operator()(const std::pair<InternalKey, std::string>& a,
                    const std::pair<InternalKey, std::string>& b) const {
        return InternalKeyComparator{}(a.first, b.first);
    }
};

class MemTable {
public:
    void Put(const std::string& user_key, const std::string& value, uint64_t seq);
    void Delete(const std::string& user_key, uint64_t seq);
    std::optional<std::string> Get(const std::string& user_key) const;
    void Dump() const;

private:
    using KV = std::pair<InternalKey, std::string>;
    SkipList<KV> skiplist_;
};

#endif  // MEMTABLE_HH_
