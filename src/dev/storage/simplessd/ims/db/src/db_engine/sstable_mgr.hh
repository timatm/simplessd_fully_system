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
#include <string_view>

#include "def.hh"
#include "internal_key.hh"
#include "skiplist.hh"
#include "record.hh"
#include "memtable.hh"
#include "tree.hh"
#include "thread.hh"
#include "nvme_interface.hh"
#include "lsmtree.hh"
#include "iterator.hh"
#include "status.hh"
#include "log_manager.hh"


static inline void* allocateAligned(size_t size) {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, 4096, size) != 0 || ptr == nullptr) {
        throw std::bad_alloc();
    }
    std::memset(ptr, 0, size);
    return ptr;
}

class SstableManager {
public:
    SstableManager(INVMEDriver& nvme,LSMTree& tree):lsmTree_(tree) ,nvme_(nvme) {}
    ~SstableManager() = default;
    // TODO
    void init();
    void readSSTable(const std::string& filename,char *buffer);
    void writeSSTable(uint8_t level,InternalKey minKey ,InternalKey maxKey,std::string sstable_buffer);
    void deleteSSTable(const std::string& filename);
    std::string packingTable(const SkipList<Record,RecordComparator> &skiplist);
    void setSequenceNumber(uint32_t seq) {
        sequenceNumber_ = seq;
    }
    void dump(){
        lsmTree_.dump_lsmtere();
    }
    void waitAllTasksDone() {
        thread_pool_.WaitForAll();
    }
private:
    int packing_type_ = static_cast<int>(PackingType::kKeyPerPage);
    ThreadPool thread_pool_{1};
    LSMTree& lsmTree_;
    mutable std::mutex tree_mutex_;
    INVMEDriver& nvme_;
    std::atomic<uint32_t> sequenceNumber_ = 0; // Sequence number for SSTables
    std::unordered_map<std::string, std::shared_ptr<std::deque<InternalKey>>> keyRangeMap; // sstable name -> key range per slot
    std::string generateFilename(uint32_t seq);
    char* keyPerPagePacking(const SkipList<Record,RecordComparator> &skiplist);
    char* keyHashPacking(const SkipList<Record,RecordComparator> &skiplist);
    char* keyRangePacking(const SkipList<Record,RecordComparator> &skiplist);
};

class SstableIterator : public InternalIterator{
public:
    struct EntryRef {
        uint32_t key_off;
    };

    SstableIterator(SstableManager* smgr,
                    LOG_MANAGER* lmgr,
                    const InternalKeyComparator* icmp,
                    std::string filename,
                    PackingType type)
        : sstable_mgr_(smgr),log_mgr_(lmgr) ,icmp_(icmp), filename_(std::move(filename)),type_(type) {
            buf_ = static_cast<char*>(allocateAligned(BLOCK_SIZE));
        }

    Status Init();
    bool Valid() const override;
    void SeekToFirst() override;
    void SeekToLast()  override;

    void Seek(std::string_view internal_target) override;

    void Next() override;
    void Prev() override;

    std::string_view key()   const override;
    bool SupportsValueCopy() const override { return true; }
    Status ReadValue(std::string& /*out*/) const override ;

    Status status() const override;


private:
    std::string_view entry_key(const EntryRef& e) const {
        return std::string_view(buf_+ e.key_off, sizeof(InternalKey));
    }
    std::vector<EntryRef> gen_sorted_view();
    std::string filename_;        
    SstableManager* sstable_mgr_{nullptr};
    LOG_MANAGER* log_mgr_{nullptr};
    const InternalKeyComparator* icmp_{nullptr};
    char* buf_;
    std::vector<EntryRef> entries_;
    int pos_ = -1;
    Status st_;
    PackingType type_;
};



#endif // __SSTABLE_MGR_HH__