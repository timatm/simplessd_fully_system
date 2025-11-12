#include "log.hh"


#include "def.hh"
#include "lbn_pool.hh"
#include "print.hh"
#include "persistence.hh"
#include "mapping_table.hh"
#include "IMS_interface.hh"

int Log::init_logRecordList(uint64_t logStoreLBN, uint64_t page_num) {
    if (logStoreLBN == INVALIDLBN) {
        pr_info("Invalid log store LBN(%lu), cannot initialize log record list", logStoreLBN);
        return OPERATION_FAILURE;
    }

    uint64_t lpn = LBN2LPN(logStoreLBN);
    uint8_t* buffer = (uint8_t*)malloc(IMS_PAGE_SIZE);
    if (!buffer) {
        pr_info("Allocating memory for log record list buffer failed");
        return OPERATION_FAILURE;
    }

    for (int page = 0; page < page_num; ++page) {
        if (persistence_.pDisk_->readPage(lpn++, buffer)) {
            pr_info("Reading log record list from disk failed");
            free(buffer);
            return OPERATION_FAILURE;
        }

        auto* logRecordPtr = reinterpret_cast<logLBNListRecord*>(buffer);
        for (int i = 0; i < IMS_PAGE_SIZE / sizeof(uint64_t); ++i) {
            if (logRecordPtr->lbn[i] != INVALID_32 && logRecordPtr->lbn[i] != 0) {
                logRecordList.push_back(logRecordPtr->lbn[i]);
            }
        }
    }

    free(buffer);
    return OPERATION_SUCCESS;
}


void Log::insert_logRecord(uint64_t lbn) {
    logRecordList.push_back(lbn);
    lbnPool_.remove_freeLBNList(lbn);
    lbnPool_.insert_usedLBNList(lbn);
}

void Log::remove_logRecord_head() {
    if (logRecordList.empty()) {
        pr_info("Log record is empty, nothing to remove");
        return;
    }

    uint64_t headLBN = logRecordList.front();
    logRecordList.pop_front();

    lbnPool_.remove_usedLBNList(headLBN);
    lbnPool_.insert_freeLBNList(headLBN);
}

int Log::flush_logRecordList() {
    if (logRecordList.empty()) {
        pr_info("Log record list is empty, nothing to flush");
        return OPERATION_SUCCESS;
    }

    uint32_t lpn = LBN2LPN(sp_old_->log_store);
    uint8_t* buffer = (uint8_t*)malloc(IMS_PAGE_SIZE);
    if (!buffer) return OPERATION_FAILURE;

    memset(buffer, 0, IMS_PAGE_SIZE);
    auto* logRecordPtr = reinterpret_cast<logLBNListRecord*>(buffer);
    int index = 0;

    while (!logRecordList.empty()) {
        uint32_t lbn = logRecordList.front();
        logRecordList.pop_front();

        if (lbn == INVALID_32) {
            pr_info("Invalid LBN encountered in log record list, skipping");
            continue;
        }

        logRecordPtr->lbn[index++] = lbn;

        if (index == IMS_PAGE_SIZE / sizeof(uint32_t)) {
            if (persistence_.pDisk_->writePage(lpn++, buffer)) {
                pr_info("Flush failed at full log page");
                free(buffer);
                return OPERATION_FAILURE;
            }
            sp_new_->log_page_num++;
            pr_info("Flushed full log page at LPN: %lu", lpn - 1);
            index = 0;
            memset(buffer, 0, IMS_PAGE_SIZE);
        }
    }

    if (index > 0) {
        if (persistence_.pDisk_->writePage(lpn++, buffer)) {
            pr_info("Flush failed at final log page");
            free(buffer);
            return OPERATION_FAILURE;
        }
        sp_new_->log_page_num++;
        pr_info("Flushed final log page at LBN: %lu (num of page: %lu)", sp_old_->log_store,sp_new_->log_page_num);
    }

    free(buffer);
    return OPERATION_SUCCESS;
}
void Log::dump() const {
    pr_info("");
    pr_info("[Log Dump]");
    pr_info("  currentLogLBN: %lu",currentLogLBN);
    pr_info("  nextLogLBN:    %lu",nextLogLBN);
    pr_info("  logOffset:     %lu",logOffset);
    pr_info("  logRecordList (%lu entries): ",logRecordList.size());
    int count = 0;
    std::string line;
    for (auto lbn : logRecordList) {
        line += std::to_string(lbn) + " ";
        count++;

        if (count % 16 == 0) {
            pr_info("%s", line.c_str());
            line.clear();
        }
    }

    if (!line.empty()) {
        pr_info("%s", line.c_str());
    }
}

void Log::clear(){
    logRecordList.clear();
}

std::string Log::encode() const {
    std::string buf;
    for (uint32_t val : logRecordList) {
        for (int i = 0; i < 4; ++i) {
            buf += static_cast<char>((val >> (i * 8)) & 0xFF);  // Little Endian
        }
    }
    return buf;
}

bool Log::decode(const std::string& buf) {
    if (buf.size() % 4 != 0) return false;
    logRecordList.clear();

    for (size_t i = 0; i < buf.size(); i += 4) {
        uint32_t val = 0;
        for (int j = 0; j < 4; ++j) {
            val |= static_cast<uint8_t>(buf[i + j]) << (j * 8);  // Little Endian
        }
        logRecordList.push_back(val);
    }
    return true;
}
