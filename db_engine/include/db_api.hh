#ifndef __DB_API__HH__
#define __DB_API__HH__

#include <cstdint>
#include <atomic>
#include <string>
#include <memory>
#include "status.hh"
#include "memtable.hh"
#include "def.hh"
#include "sstable_mgr.hh"
#include "options.hh"
#include "nvme_interface.hh"
#include "read_cache.hh"
#include "sstable_format_idxbf.hh"

// Forward declaration of MemTable
class MemTable;


class API {
public:
    API();
    ~API() = default;
    std::unique_ptr<INVMEDriver> nvme_;
    // std::unique_ptr<ReadCache> read_cache_;
    std::unique_ptr<ReadCache> keyRangeCache_;

    Status open();
    Status get(std::string key ,std::string& value);
    Status get(std::string key,Record& value);
    Status delete_key(std::string key ,std::string value);
    Status put(std::string key ,std::string value);
    Status put_from_gc(std::string key ,std::string value);
    
    Status search(std::string key ,std::string& value);
    Status range_query(std::string start_key, std::string end_key, std::set<std::string>& result_set);
    Status scan(std::string start_key,int count, std::set<std::string>& result_set);
    Status close();

    Status removeSSable(std::shared_ptr<TreeNode> rm);
    void dump_system();
    void dump_memtable();
    void dump_lsmtree();
    void dump_log_manager();
    void dump_all();
    MemTable* getMemTable(){return memtable_.get();}
    MemTable* getImmutMemTable(){return immutable_memtable_.get();}
    LogManager* getLogManager(){return logManager_.get();}
    SstableManager* getSSTable(){return sstableManager_.get();}
    LSMTree* getLSMTree(){return lsmTree_.get();}
    PackingType getPackType(){return packing_;}
    std::set<InternalKey,SetComparator> parse_sstable(char *);
    

    std::optional<std::set<std::string>> read_key_range(const std::string& filename);
    SearchPatternD generate_SearchPatternD(const std::string& filename, const Key& key,const std::set<std::string>& keys);
    SearchPatternH generate_SearchPatternH(const std::string& filename, const Key& key,const std::set<std::string>& keys);
    // void generate_search_package(const std::string& filename, const std::string& pattern);
    std::set<InternalKey ,SetComparator> parse_sstable_page(char* buffer);

    void init_compaction_key_list();
    void set_compaction_key_list(InternalKey key , int level);
    void test();

    
private:
    void SimulateDeviceIOIfNeeded(const std::vector<std::shared_ptr<TreeNode>> &srcNodes,const std::vector<std::shared_ptr<TreeNode>> &dstNodes);
    Status put_impl(std::string key ,std::string value,PutType);

    void OnSSTableFlushed(const sstable_info& info);
    void OnSSTableWriteFailed(const sstable_info& info, int err);
    void compaction();
    bool compactionTrigger(int level);
    void log_garbage_collection();
    void print_result();
private:
    std::shared_ptr<Tree> tree_;
    std::unique_ptr<LSMTree> lsmTree_;
    PackingType packing_;
    std::unique_ptr<MemTable> memtable_;
    std::shared_ptr<MemTable> immutable_memtable_;
    std::unique_ptr<LogManager> logManager_;
    std::atomic<uint64_t> global_seq_{0}; 
    std::unique_ptr<SstableManager> sstableManager_;
    std::vector<std::optional<InternalKey>> compaction_key_list_;
    InternalKeyComparator icmp_;
    std::mutex mu_;

    double total_space_util             = 0.0;
    double total_SStable_num            = 0.0;
    double total_cache_read_count       = 0.0;
    double total_cache_read_miss_count  = 0.0;

    uint32_t total_put_count = 0;
    uint32_t total_get_count = 0;

    uint64_t search_pattern_io = 0;
    uint32_t compaction_count = 0;
    uint32_t search_hit_in_memory = 0;

    uint32_t sstable_write_count_immtable = 0;
    uint32_t sstable_write_count_compaction = 0;
};
#endif