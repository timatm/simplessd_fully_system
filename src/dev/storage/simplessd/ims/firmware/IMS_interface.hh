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

#include "log.hh"
#include "mapping_table.hh"
#include "persistence.hh"
#include "disk.hh"
#include "lbn_pool.hh"
#include "tree.hh"
#include "def.hh"
#include "lsmtree.hh"

static constexpr size_t kDefaultDeviceDramSize = 1ULL << 30; // 1 GiB
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
    uint8_t* buffer_;
    size_t buffer_size_;
    size_t buffer_valid_size_;
#if RUNTYPE_SIMPLESSD
    SimpleSSD::Disk disk_;
#else
    Disk disk_;
#endif
    IMS_interface();
    int rebuild_super_page();
    int write_sstable(uint8_t *buffer);
    int read_sstable(uint8_t *buffer);
    int erase_sstable();
    int read_ssKeyRange(hostInfo *request, uint8_t* buffer);
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


    int close_DB(uint8_t *host_buffer, size_t size);
    int open_DB(uint32_t *datalen);
    

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
    super_page* get_oldSuperPage() {return sp_ptr_old_;}

    void dump_mapping(){mappingTable_->dump_mapping(); }
    void dump_lbn_pool(){lbnPool_->dump_LBNPool();}
    void dump_log_mannger(){logManager_->dump();}
    void dump_lsm_tree(){tree_->dump();}
    void dump_all(){
        dump_mapping();
        dump_lbn_pool();
        dump_log_mannger();
        dump_lsm_tree();
    }


    int reset_IMS_buffer();
private:
    std::mutex buf_mu_;
    void alloc_device_buffer(size_t bytes);
    void free_device_buffer();
    bool in_range(uint64_t off, size_t len) const {
        return (off <= buffer_size_) && (len <= buffer_size_) &&
               (off + len <= buffer_size_);
    }


private:
    void reset_superPage(super_page*);
    std::shared_ptr<Tree> tree_;
    std::unique_ptr<Log> logManager_;
    std::unique_ptr<Mapping> mappingTable_;
    std::unique_ptr<Persistence> persistenceManager_;
    std::unique_ptr<LBNPool> lbnPool_;
    std::unique_ptr<LSMTree> lsmTree_;
    super_page* sp_ptr_old_ = nullptr;
    super_page* sp_ptr_new_ = nullptr;
    

};



#endif