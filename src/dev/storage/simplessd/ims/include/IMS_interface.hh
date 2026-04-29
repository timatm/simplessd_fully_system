#ifndef __IMS_INTERFACE__HH__
#define  __IMS_INTERFACE__HH__


#include <string>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <cstdint>

#include <vector>
#include <iostream>

#include <optional>
#include <stdexcept>
#include <mutex>

#include "../include/log.hh"
#include "mapping_table.hh"
#include "persistence.hh"
#include "disk.hh"
#include "lbn_pool.hh"
#include "tree.hh"
#include "def.hh"
#include "lsmtree.hh"


#if RUNTYPE
#include "util/disk.hh"
#endif
static constexpr size_t kDefaultDeviceDramSize = 50ULL << 20;
static constexpr size_t kAlign = 4096;
namespace {
    inline void* aligned_alloc_4k(size_t bytes) {
    #if defined(_MSC_VER)
        return _aligned_malloc(bytes, kAlign);
    #else
        void* p = nullptr;
        if (posix_memalign(&p, kAlign, bytes) != 0) return nullptr;
        return p;
    #endif
    }
    inline void aligned_free_4k(void* p) {
    #if defined(_MSC_VER)
        _aligned_free(p);
    #else
        free(p);
    #endif
    }
} // anonymous

class IMS_interface{
public:
    uint8_t* buffer_ = nullptr;
    size_t buffer_size_;
    size_t buffer_valid_size_;
#if RUNTYPE
    SimpleSSD::Disk* disk_ = nullptr;  // <--- pointer
#else
    Disk disk_;
#endif

    IMS_interface();
    ~IMS_interface();
    int rebuild_super_page();
    int write_sstable(uint64_t &lbn,bool isCompaction);
    int read_sstable(uint64_t &lbn);
    int erase_sstable(uint64_t &lbn);
    int read_ssKeyRange(uint64_t &lpn);
    int read_ssPage(uint64_t &lpn);
    // TODO need to finish
    int search_key(Key key);

    int write_meta(uint8_t *host_buffer, size_t size);
    int read_meta(uint8_t *host_uffer, size_t size);

    int allocate_block(uint64_t *);
    int write_log(uint64_t,uint8_t *buffer);
    int read_log(uint64_t,uint8_t *buffer);
    int write_block(uint32_t lbn, uint8_t* buffer);
    int read_block(uint32_t lbn, uint8_t* buffer);

    int set_sstable_info(uint32_t *size);
    int set_log_info(uint32_t *size);
    int trivial_move();
    int simulate_compaction_io(std::vector<uint64_t> &pbn_list);
    int close_DB(uint8_t *host_buffer, size_t size);
    int open_DB(uint32_t *datalen);
    int search(std::vector<uint64_t> &pbn_list);

    int search_from_buffer(const uint8_t* data,
                                      size_t size,
                                      std::vector<uint64_t>& pbn_list);
    int init_IMS();
    int close_IMS();
    int reset_IMS();
    int init_DB(uint8_t *buffer);
    int dump_IMS();
    // super_page* get_super_page_old() { return sp_ptr_old_; }
    // super_page* get_super_page_new() { return sp_ptr_new_; }
    Persistence* get_persistenceManager() {return persistenceManager_.get() ;}
    Log* get_logManager() {return logManager_.get() ;}
    LSMTree* get_lsmTree() {return lsmTree_.get() ;}
    super_page* get_superPage() {return sp_ptr_;}
    LBNPool* get_lbnpool() {return lbnPool_.get();}

    void dump_mapping(){mappingTable_->dump_mapping(); }
    void dump_lbn_pool(){lbnPool_->dump_LBNPool();}
    void dump_log_mannger(){logManager_->dump();}
    void dump_lsm_tree(){tree_->dump();}
    void dump_all(){
        dump_mapping();
        // dump_lbn_pool();
        dump_log_mannger();
        dump_lsm_tree();
    }
    int reset_IMS_buffer();
    #if RUNTYPE
        void attachDisk(SimpleSSD::Disk* d);
    #endif
private:
    std::mutex buf_mu_;
    void alloc_device_buffer(size_t bytes);
    void free_device_buffer();
    bool in_range(uint64_t off, size_t len) const {
        return (off <= buffer_size_) && (len <= buffer_size_) &&
               (off + len <= buffer_size_);
    }
    void print_result();

private:
    void reset_superPage(super_page*);
    std::shared_ptr<Tree> tree_;
    std::unique_ptr<Log> logManager_;
    std::unique_ptr<Mapping> mappingTable_;
    std::unique_ptr<Persistence> persistenceManager_;
    std::unique_ptr<LBNPool> lbnPool_;
    std::unique_ptr<LSMTree> lsmTree_;
    super_page* sp_ptr_ = nullptr;
    bool closed_ = false;
    std::vector<uint32_t> sstable_count_per_ch;
    uint32_t total_sstable_write_count = 0;
    uint32_t total_search_parallel_block_num = 0;
    uint32_t total_search_count = 0;
    uint32_t total_compaction_parallel_block_num = 0;
    uint32_t total_compaction_count = 0;
    // uint32_t compaction_count = 0;
};

#endif