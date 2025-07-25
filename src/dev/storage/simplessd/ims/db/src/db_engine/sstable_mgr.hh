#ifndef __SSTABLE_MGR_HH__
#define __SSTABLE_MGR_HH__
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <set>
#include <memory>
#include "def.hh"
#include "internal_key.hh"
#include "skiplist.hh"
#include "record.hh"
#include "memtable.hh"
#include "tree.hh"
#include "thread.hh"
class SstableManager {
public:
    SstableManager() = default;
    ~SstableManager() = default;
    // TODO
    void init();
    void readSSTable(const std::string& filename);
    void writeSSTable(uint8_t level,InternalKey minKey ,InternalKey maxKey,char * sstable_buffer);
    void deleteSSTable(const std::string& filename);
    std::string packingTable(const SkipList<Record,RecordComparator> &skiplist);
private:
    int packing_type_ = static_cast<int>(PackingType::kKeyPerPage);
    ThreadPool thread_pool_{1};
    Tree lsmTree_;
    uint32_t sequenceNumber_ = 0; // Sequence number for SSTables
    std::unordered_map<std::string, std::shared_ptr<std::deque<InternalKey>>> keyRangeMap; // sstable name -> key range per slot
    std::string generateFilename(uint32_t seq);
    char * keyPerPagePacking(const SkipList<Record,RecordComparator> &skiplist);
    char * keyHashPacking(const SkipList<Record,RecordComparator> &skiplist);
    char * keyRangePacking(const SkipList<Record,RecordComparator> &skiplist);
};

#endif // __SSTABLE_MGR_HH__