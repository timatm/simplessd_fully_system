#ifndef __LOG_MANAGER_HH__
#define __LOG_MANAGER_HH__

#include <cstdint>
#include <string>
#include <vector>
#include "record.hh"
#include "def.hh"
class LOG_MANAGER {

public:
    LOG_MANAGER() = default;
    ~LOG_MANAGER() = default;
    
    // Add methods for log management, such as writing logs, reading logs, etc.
    void writeLog(const Record& log);
    Record* readLog(uint32_t lpn,uint32_t offset,std::string buffer) const;
    void getLPN(uint32_t & current_lpn, uint32_t & byte_offset) const;
    void ClearLog();
private:
    // Internal storage for logs, could be a vector or a file-based system
    uint32_t next_lbn_ ;
    uint32_t currenet_lbn_ ;
    uint32_t page_offset_ ;
    uint32_t byte_offset_ ;
};

#endif  // __LOG_MANAGER_HH__