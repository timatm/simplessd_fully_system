#ifndef __DB_API__HH__
#define __DB_API__HH__

#include <cstdint>
#include <atomic>
#include <string>
#include <memory>
#include "status.hh"
#include "log_manager.hh"
#include "memtable.hh"
// Forward declaration of MemTable
class MemTable;


class API {
public:

    void get();
    Status put(std::string key ,std::string value);
private:
    std::unique_ptr<MemTable> memtable_;
    std::unique_ptr<MemTable> immutable_memtable_;
    LOG_MANAGER logManager_;
    std::atomic<uint64_t> global_seq_{0}; 
};
#endif