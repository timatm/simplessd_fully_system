#ifndef __DB_API__HH__
#define __DB_API__HH__

#include <cstdint>
#include <atomic>
#include <string>
#include <memory>
#include "status.hh"
#include "log_manager.hh"
#include "memtable.hh"
#include "def.hh"
#include "sstable_mgr.hh"
#include "options.hh"
#include "nvme_interface.hh"
#include "read_cache.hh"
// Forward declaration of MemTable
class MemTable;


class API {
public:
    API();
    ~API() = default;
    std::unique_ptr<INVMEDriver> nvme_;
    std::unique_ptr<ReadCache> read_cache_;

    Status open();
    Status get(std::string key ,std::string& vlaue);
    Status delete_key(std::string key ,std::string vlaue);
    Status put(std::string key ,std::string value);
    Status search(std::string key ,std::string& vlaue);
    Status range_query(std::string start_key, std::string end_key, std::set<std::string>& result_set);

    void dump_system();
    void dump_memtable();
    void dump_lsmtree();
    void dump_log_manager();
    void dump_all();
    MemTable* getMemTable(){return memtable_.get();}
    MemTable* getImmutMemTable(){return immutable_memtable_.get();}
    LOG_MANAGER* getLogManager(){return logManager_.get();}
    SstableManager* getSSTable(){return sstableManager_.get();}
    LSMTree* getLSMTree(){return lsmTree_.get();}
    std::set<InternalKey,SetComparator> parse_sstable(char *);

    std::set<std::string> read_key_range(const std::string& filename);
    SearchPattern generate_search_slot(const std::string& filename, const Key& key,const std::set<std::string>& keys);
    void generate_search_package(const std::string& filename, const std::string& pattern);
    std::set<InternalKey ,SetComparator> parse_sstable_page(char* buffer);

private:
    std::shared_ptr<Tree> tree_;
    std::unique_ptr<LSMTree> lsmTree_;
    PackingType packing_;
    std::unique_ptr<MemTable> memtable_;
    std::unique_ptr<MemTable> immutable_memtable_;
    std::unique_ptr<LOG_MANAGER> logManager_;
    std::atomic<uint64_t> global_seq_{0}; 
    std::unique_ptr<SstableManager> sstableManager_;
    
};
#endif