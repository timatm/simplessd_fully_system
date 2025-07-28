#ifndef LOG_MANAGER_HH
#define LOG_MANAGER_HH

#include <deque>
#include <cstdint>
#include <string>
#include "record.hh"
#include "nvme_interface.hh"
class LOG_MANAGER {
public:
    LOG_MANAGER(NVMe& nvme);
    ~LOG_MANAGER() = default;
    //TODO
    void init();
    void writeLog(const Record& log);
    Record* readLog(uint32_t lpn, uint32_t offset) const;
    void getLPN(uint32_t& current_lpn, uint32_t& byte_offset) const;
    void ClearLog();

private:
    std::deque<uint32_t> logRecordBlock_;
    uint32_t next_lbn_;
    uint32_t currenet_lbn_;
    uint32_t page_offset_;
    uint32_t byte_offset_;
    std::string buffer_;
    void allocate_lbn();  // 模擬向裝置請求新的 LBN
    void flush_buffer();
    NVMe& nvme_;
};

#endif  // LOG_MANAGER_HH
