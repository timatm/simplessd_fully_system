#include "log.hh"


#include "def.hh"
#include "lbn_pool.hh"
#include "print.hh"
#include "persistence.hh"
#include "mapping_table.hh"
#include "IMS_interface.hh"
int Log::init_logRecordList(uint64_t logStoreLBN,uint64_t page_num){
    int err = OPERATION_FAILURE;
    if(logStoreLBN == INVALIDLBN) {
        pr_info("Invalid log store LBN, cannot initialize log record list");
        return OPERATION_FAILURE;
    }
    uint64_t lpn = LBN2LPN(logStoreLBN);
    uint8_t *buffer = (uint8_t *)malloc(IMS_PAGE_SIZE);
    if(!buffer){
        pr_info("Allocating memory for log record list buffer failed");
        return OPERATION_FAILURE;
    }
    for(int page = 0; page < page_num; page++) {
        err = persistenceManager.disk.read(lpn, buffer);
        if(err == OPERATION_FAILURE) {
            pr_info("Reading log record list from disk failed at LPN: %lu", lpn);
            free(buffer);
            return OPERATION_FAILURE;
        }
        logLBNListRecord *logRecordPtr = (logLBNListRecord *)buffer;
        for (int i = 0; i < IMS_PAGE_SIZE / sizeof(uint64_t); i++) {
            if (logRecordPtr->lbn[i] != INVALIDLBN) {
                logRecordList.push_back(logRecordPtr->lbn[i]);
            }
        }
        lpn++;
    }
    free(buffer);
    return OPERATION_SUCCESS;
}   

void Log::insert_logRecord(uint64_t lbn) {
    logRecordList.push_back(lbn);
    lbnPoolManager.remove_freeLBNList(lbn);
    lbnPoolManager.insert_usedLBNList(lbn);
    return ;
}

void Log::remove_logRecord_head() {
    if (logRecordList.empty()) {
        pr_info("Log record is empty, nothing to remove");
        return;
    }
    uint64_t headLBN = logRecordList.front();
    logRecordList.pop_front();
    lbnPoolManager.remove_usedLBNList(headLBN);
    lbnPoolManager.insert_freeLBNList(headLBN);
    return ;
}

int Log::flush_logRecordList() {
    if (logRecordList.empty()) {
        pr_info("Log record list is empty, nothing to flush");
        return OPERATION_SUCCESS;
    }

    uint64_t lbn = sp_ptr_old->log_store;
    uint64_t lpn = LBN2LPN(lbn);
    uint8_t *buffer = (uint8_t *)malloc(IMS_PAGE_SIZE);
    if (!buffer) {
        pr_info("Allocating memory for log record list buffer failed");
        return OPERATION_FAILURE;
    }

    memset(buffer, 0, IMS_PAGE_SIZE);
    logLBNListRecord *logRecordPtr = (logLBNListRecord *)buffer;
    int index = 0;

    while (!logRecordList.empty()) {
        uint64_t lbn = logRecordList.front();

        if (lbn == INVALIDLBN) {
            pr_info("Invalid LBN encountered in log record list, skipping");
            logRecordList.pop_front();  // ✅ 避免死循環
            continue;
        }

        logRecordPtr->lbn[index++] = lbn;
        logRecordList.pop_front();

        if (index == IMS_PAGE_SIZE / sizeof(uint64_t)) {
            int err = persistenceManager.disk.write(lpn, buffer);
            if (err == OPERATION_FAILURE) {
                pr_info("Flushing log record list to disk failed at LPN: %lu", lpn);
                free(buffer);
                return OPERATION_FAILURE;
            }
            pr_info("Flushed log page at LPN: %lu", lpn);
            lpn++;
            sp_ptr_new->log_page_num++;
            index = 0;
            memset(buffer, 0, IMS_PAGE_SIZE);
            logRecordPtr = (logLBNListRecord *)buffer;  // ✅ reset 指標
        }
    }

    if (index > 0) {
        int err = persistenceManager.disk.write(lpn, buffer);
        if (err == OPERATION_FAILURE) {
            pr_info("Flushing remaining log record list to disk failed at LPN: %lu", lpn);
            free(buffer);
            return OPERATION_FAILURE;
        }
        pr_info("Flushed final partial log page at LPN: %lu", lpn);
        sp_ptr_new->log_page_num++;
    }

    free(buffer);
    return OPERATION_SUCCESS;
}

void Log::clear(){
    logRecordList.clear();
}
