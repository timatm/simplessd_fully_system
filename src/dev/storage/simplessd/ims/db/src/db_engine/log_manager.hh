#ifndef LOG_MANAGER_HH
#define LOG_MANAGER_HH

#include <deque>
#include <cstdint>
#include <string>
#include "record.hh"
#include "nvme_interface.hh"
#include <optional>
class LOG_MANAGER {
public:
    LOG_MANAGER(INVMEDriver& nvme);
    ~LOG_MANAGER() = default;
    //TODO
    void writeLog(const Record& log);
    std::optional<Record> readLog(uint32_t lpn, uint32_t offset);
    void getLPN(uint32_t& current_lpn, uint32_t& byte_offset) const;
    uint32_t getLPN() const;
    void setNextLBN(uint32_t next_lbn) { next_lbn_ = next_lbn; }
    void setCurrentLBN(uint32_t current_lbn) { currenet_lbn_ = current_lbn; }
    void setPageOffset(uint32_t page_offset) { page_offset_ = page_offset; }
    void clearLog();
    bool decode(const std::string& buf);
    void dump() const;
    void flush_buffer() ;
private:
    uint32_t findNextLPN(uint32_t lpn) const;
    std::deque<uint32_t> logRecordBlock_;
    uint32_t next_lbn_;
    uint32_t currenet_lbn_;
    uint32_t page_offset_;
    uint32_t byte_offset_;
    std::string buffer_;
    void allocate_lbn();
    
    INVMEDriver& nvme_;
};

#endif  // LOG_MANAGER_HH
