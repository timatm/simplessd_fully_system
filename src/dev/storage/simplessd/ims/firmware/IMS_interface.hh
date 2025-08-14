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

#include "log.hh"
#include "mapping_table.hh"
#include "persistence.hh"
#include "disk.hh"
#include "lbn_pool.hh"
#include "tree.hh"
#include "def.hh"
#include "lsmtree.hh"

class IMS_interface{
public:
#if RUNTYPE_SIMPLESSD
    SimpleSSD::Disk disk_;
#else
    Disk disk_;
#endif
    IMS_interface();
    int rebuild_super_page();
    int write_sstable(hostInfo *request,uint8_t *buffer);
    int read_sstable(hostInfo *request ,uint8_t *buffer);
    int read_ssKeyRange(hostInfo *request, uint8_t* buffer);
    // TODO need to finish
    int search_key(Key key);
    int write_metadata(uint8_t *buffer, size_t size);


    int allocate_block(uint64_t *);
    int write_log(uint64_t,uint8_t *buffer);
    int read_log(uint64_t,uint8_t *buffer);
    int close_DB();
    int open_DB(uint8_t* buffer, size_t buffer_size);
    

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