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
class SstableManager {
public:
    SstableManager() = default;
    ~SstableManager() = default;
    // TODO
    void init();
    void readSSTable(const std::string& filename);
    void writeSSTable(char * sstable_buffer);
    void deleteSSTable(const std::string& filename);
    char * keyPerPagePacking(SkipList<Record,RecordComparator> &skiplist);
    char * keyHashPacking(SkipList<Record,RecordComparator> &skiplist);
    char * keyRangePacking(SkipList<Record,RecordComparator> &skiplist);
private:
    uint32_t sequenceNumber_ = 0; // Sequence number for SSTables
    std::unordered_map<std::string, std::shared_ptr<std::deque<InternalKey>>> keyRangeMap; // sstable name -> key range per slot
    std::string generateFilename(uint32_t seq); 
};

#endif // __SSTABLE_MGR_HH__