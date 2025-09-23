#ifndef LOG_MANAGER_HH
#define LOG_MANAGER_HH

#include <deque>
#include <cstdint>
#include <string>
#include "record.hh"
#include "nvme_interface.hh"
#include "def.hh"
#include "status.hh"
#include "iterator.hh"
#include <optional>


class LogManager {
public:
    LogManager(INVMEDriver& nvme);
    ~LogManager() = default;
    //TODO
    void init();

    void writeLog(const Record& log);
    std::optional<Record> readLog(uint32_t lpn, uint32_t offset);
    std::vector<Record> readLogBlock(uint32_t lbn,uint32_t valid_offset,uint32_t &nextBlockValidOffset);
    void getLPN(uint32_t& current_lpn, uint32_t& byte_offset) const;
    uint32_t getLPN() const;

    void clearLog();
    bool decode(const std::string& buf);
    void dump() const;
    void flush_buffer() ;
    int get_log_block_num() const { return logRecordBlock_.size(); }
    uint32_t get_log_list_front()const { return logRecordBlock_.front(); }


    void setNextLBN(uint32_t next_lbn) { next_lbn_ = next_lbn; }
    void setCurrentLBN(uint32_t current_lbn) { currenet_lbn_ = current_lbn; }
    void setPageOffset(uint32_t page_offset) { page_offset_ = page_offset; }
    void setByteOffset(uint32_t byte_offset) { byte_offset_ = byte_offset; }
    void setFirstBlockOffset(uint32_t first_block_offset) { first_block_offset_ = first_block_offset; }

    uint32_t get_first_block_offset() const { return first_block_offset_; }
    uint32_t get_page_offset() const { return page_offset_; }
    uint32_t get_byte_offset() const { return byte_offset_; }

    void set_first_block_offset_(uint32_t offset) { first_block_offset_ = offset; }
    void remove_log_front() { logRecordBlock_.pop_front(); }


private:
    uint32_t findNextLPN(uint32_t lpn) const;
    std::deque<uint32_t> logRecordBlock_;
    uint32_t next_lbn_;
    uint32_t currenet_lbn_;
    uint32_t page_offset_;
    uint32_t byte_offset_;
    uint32_t first_block_offset_;
    std::string buffer_;
    void allocate_lbn();
    
    INVMEDriver& nvme_;
};



#endif  // LOG_MANAGER_HH
