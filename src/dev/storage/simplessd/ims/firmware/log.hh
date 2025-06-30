#ifndef __LOG_HH__
#define __LOG_HH__

#include <string>
#include <cstdint>
#include <deque>


class Log {
public:
    Log() = default;
    ~Log() = default;
    std::deque<uint64_t> logRecordList; // Store LBNs of log records
    uint64_t currentLogLBN; // Current log LBN
    uint64_t nextLogLBN; // Next log LBN to be written
    uint64_t logOffset; // The current log records to which page
    int init_logRecordList(uint64_t logStoreLBN,uint64_t page_num);
    void insert_logRecord(uint64_t lbn);
    void remove_logRecord_head();
    int flush_logRecordList();
    void clear();
};

extern Log logManager;

#endif // __LOG_HH__