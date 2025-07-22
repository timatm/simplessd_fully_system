#ifndef MEMTABLE_HH_
#define MEMTABLE_HH_

#include <string>
#include <optional>
#include <vector>
#include "internal_key.hh"
#include "record.hh"
#include "skiplist.hh"
#include "def.hh"
enum class PackingType {
    kKeyPerPage     = 0x0,
    kHash           = 0x1,
    kKeyRange       = 0x2,
};

struct RecordComparator {
    bool operator()(const Record& a, const Record& b) const {
        InternalKeyComparator cmp;
        return cmp(a.internal_key, b.internal_key);
    }
};

class MemTable {
public:
    MemTable();

    void Put(const Record &rec);
    std::optional<std::string> Get(const std::string& user_key) const;
    void Dump() const;
    void packingTable();
    size_t ApproximateMemoryUsage() const;
    bool memTableIsFull();

private:
    std::string keyPerPagePacking();
    std::string keyHashPacking();
    std::string keyRangePacking();
    SkipList<Record,RecordComparator> skiplist_;  // SkipList 儲存 Record
    uint32_t node_count_ = 0;              // SkipList 節點數
    size_t size_ = 0;                      // 估計記憶體大小
    int packing_type_ = static_cast<int>(PackingType::kKeyPerPage);
    std::vector<uint32_t> hash_num_;
};

// 外部雜湊工具
size_t HashModN(const InternalKey& ikey, size_t n);

#endif  // MEMTABLE_HH_
