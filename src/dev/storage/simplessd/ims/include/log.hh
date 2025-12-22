#ifndef __LOG_HH__
#define __LOG_HH__

#include <string>
#include <cstdint>
#include <deque>
#include "persistence.hh"
#include "lbn_pool.hh"
#include "def.hh" 

// class Log {
// public:
//     Log() = default;
//     ~Log() = default;
//     std::deque<uint64_t> logRecordList; // Store LBNs of log records
//     uint64_t currentLogLBN; // Current log LBN
//     uint64_t nextLogLBN; // Next log LBN to be written
//     uint64_t logOffset; // The current log records to which page
//     int init_logRecordList(uint64_t logStoreLBN,uint64_t page_num);
//     void insert_logRecord(uint64_t lbn);
//     void remove_logRecord_head();
//     int flush_logRecordList();
//     void clear();
// };

class Log {
public:
    Log(Persistence& persistence, LBNPool& pool, super_page* sp_)
        : persistence_(persistence), lbnPool_(pool), sp_ptr_(sp_) {
            currentLogLBN = sp_->currentLogLBN;
            nextLogLBN = sp_->nextLogLBN;
            logOffset = sp_->logOffset;
        }

    int init_logRecordList(uint64_t logStoreLBN, uint64_t page_num);
    void insert_logRecord(uint64_t lbn);
    void remove_logRecord_head();
    int flush_logRecordList();
    std::string encode() const;
    bool decode(const std::string& buf);
    void clear();
    void dump() const;
    std::deque<uint32_t> logRecordList;
    uint32_t currentLogLBN = INVALID_32;
    uint32_t nextLogLBN = INVALID_32;
    uint32_t logOffset = 0;

private:
    Persistence& persistence_;
    LBNPool& lbnPool_;
    super_page* sp_ptr_;
};

#endif // __LOG_HH__