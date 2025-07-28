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
// Forward declaration of MemTable
class MemTable;


class API {
public:
    API();
    ~API() = default;
    std::unique_ptr<NVMe> nvme_;
    void get();
    Status put(std::string key ,std::string value);
    void dump_memtable();
    void dump_lsmtree();

    MemTable* getMemTable(){return memtable_.get();}
    MemTable* getImmutMemTable(){return immutable_memtable_.get();}
    LOG_MANAGER* getLogManager(){return logManager_.get();}
    SstableManager* getSSTable(){return sstableManager_.get();}

private:
    
    PackingType packing_;
    std::unique_ptr<MemTable> memtable_;
    std::unique_ptr<MemTable> immutable_memtable_;
    std::unique_ptr<LOG_MANAGER> logManager_;
    std::atomic<uint64_t> global_seq_{0}; 
    std::unique_ptr<SstableManager> sstableManager_;

};
#endif