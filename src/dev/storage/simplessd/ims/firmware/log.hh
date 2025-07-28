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
    Log(Persistence& persistence, LBNPool& pool, super_page* sp_old, super_page* sp_new)
        : persistence_(persistence), lbnPool_(pool), sp_old_(sp_old), sp_new_(sp_new) {
            currentLogLBN = sp_old_->currentLogLBN;
            nextLogLBN = sp_old_->nextLogLBN;
            logOffset = sp_old_->logOffset;
        }

    int init_logRecordList(uint64_t logStoreLBN, uint64_t page_num);
    void insert_logRecord(uint64_t lbn);
    void remove_logRecord_head();
    int flush_logRecordList();
    void clear();
    void dump() const;
    std::deque<uint64_t> logRecordList;
    uint64_t currentLogLBN = INVALIDLBN;
    uint64_t nextLogLBN = INVALIDLBN;
    uint64_t logOffset = 0;

private:
    Persistence& persistence_;
    LBNPool& lbnPool_;
    super_page* sp_old_;
    super_page* sp_new_;
};

#endif // __LOG_HH__