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



static AlignedBuf MakeAlignedBlockSize() {
    void* p = nullptr;
    // 也可用 aligned_alloc(kAlign, kTableSize)；posix_memalign 更通用
    int rc = ::posix_memalign(&p, 4096, BLOCK_SIZE);
    if (rc != 0 || p == nullptr) throw std::bad_alloc();
    std::memset(p, 0xFF, BLOCK_SIZE);
    return AlignedBuf{ std::unique_ptr<void, void(*)(void*)>(p, &::free), BLOCK_SIZE, 4096 };
}

using OnWriteDone = std::function<void(const sstable_info& info)>;
using OnWriteFail = std::function<void(const sstable_info& info, int err)>;

class SstableManager {
public:
    // SstableManager(INVMEDriver& nvme,LSMTree& tree):lsmTree_(tree) ,nvme_(nvme) ,sequenceNumber_(0){};
    SstableManager(INVMEDriver& nvme, LSMTree& tree)
    : lsmTree_(tree), nvme_(nvme), sequenceNumber_(0) {
        std::cout << "[ctor] seq=" << sequenceNumber_.load(std::memory_order_relaxed) << "\n";
    }

    ~SstableManager() = default;
    // TODO
    static constexpr size_t kAlign     = 4096;             // 4KB
    static constexpr size_t kTableSize = BLOCK_SIZE;



    // 主要 API：回傳「對齊且固定 2MB」的打包結果
    AlignedBuf packingTable(const SkipList<Record, RecordComparator>& skiplist) const;

    void init();
    void readSSTable(const std::string& filename,char *buffer);
    void writeSSTable(uint8_t level,InternalKey minKey ,InternalKey maxKey,AlignedBuf sstable_buffer,bool);
    void eraseSSTable(const std::string& filename);
    // std::string packingTable(const SkipList<Record,RecordComparator> &skiplist);

    AlignedBuf packingTable(std::queue<std::string> sortedLsit);
    void setSequenceNumber(uint32_t seq) {
        sequenceNumber_ = seq;
    }

    uint32_t getSequenceNumber() {
        return sequenceNumber_;
    }
    void dump(){
        lsmTree_.dump_lsmtere();
    }
    void waitAllTasksDone() {
        thread_pool_.WaitForAll();
    }

    void set_on_write_done(OnWriteDone cb) { on_write_done_ = std::move(cb); }
    void set_on_write_fail(OnWriteFail cb) { on_write_fail_ = std::move(cb); }

private:
    ThreadPool thread_pool_{1};
    LSMTree& lsmTree_;
    mutable std::mutex tree_mutex_;
    INVMEDriver& nvme_;
    std::atomic<uint32_t> sequenceNumber_ ; // Sequence number for SSTables
    std::unordered_map<std::string, std::shared_ptr<std::deque<InternalKey>>> keyRangeMap; // sstable name -> key range per slot

    OnWriteDone on_write_done_;
    OnWriteFail on_write_fail_;

private:
    std::string generateFilename(uint32_t seq);
    // char* keyPerPagePacking(const SkipList<Record,RecordComparator> &skiplist);
    // char* keyHashPacking(const SkipList<Record,RecordComparator> &skiplist);
    // char* keyRangePacking(const SkipList<Record,RecordComparator> &skiplist);

    AlignedBuf keyPerPagePacking(const SkipList<Record, RecordComparator>& skiplist) const;
    AlignedBuf keyHashPacking   (const SkipList<Record, RecordComparator>& skiplist) const;
    AlignedBuf keyRangePacking  (const SkipList<Record, RecordComparator>& skiplist) const;

    static inline void append_bytes(AlignedBuf& buf, size_t& off, const void* src, size_t len) {
        if (off + len > buf.size) {
            throw std::runtime_error("append_bytes OOB: off=" + std::to_string(off) +
                                     " len=" + std::to_string(len) +
                                     " cap=" + std::to_string(buf.size));
        }
        std::memcpy(buf.data() + off, src, len);
        off += len;
    }

    // char* keyPerPagePacking(std::queue<std::string> sortedLsit);
    // char* keyHashPacking(std::queue<std::string> sortedLsit);
    // char* keyRangePacking(std::queue<std::string> sortedLsit);

    AlignedBuf keyPerPagePacking(std::queue<std::string> sortedLsit) const;
    AlignedBuf keyHashPacking(std::queue<std::string> sortedLsit) const;
    AlignedBuf keyRangePacking(std::queue<std::string> sortedLsit) const;

    void  notify_done(const sstable_info& info) noexcept;
    void  notify_fail(const sstable_info& info, int err) noexcept;
    PackingType packing_type_ = PACKING_T; 
};

class SstableIterator : public InternalIterator{
public:
    struct EntryRef {
        uint32_t key_off;
    };

    SstableIterator(SstableManager* smgr,
                    LogManager* lmgr,
                    const InternalKeyComparator* icmp,
                    std::string filename,
                    PackingType type)
        : sstable_mgr_(smgr),log_mgr_(lmgr) ,icmp_(icmp), filename_(std::move(filename)),type_(type) {
            buf_ = MakeAlignedBlockSize();
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
        return std::string_view(buf_.data()+ e.key_off, sizeof(InternalKey));
    }
    std::vector<EntryRef> gen_sorted_view();
    std::string filename_;        
    SstableManager* sstable_mgr_{nullptr};
    LogManager* log_mgr_{nullptr};
    const InternalKeyComparator* icmp_{nullptr};
    AlignedBuf buf_;
    std::vector<EntryRef> entries_;
    int pos_ = -1;
    Status st_;
    PackingType type_;
};



#endif // __SSTABLE_MGR_HH__