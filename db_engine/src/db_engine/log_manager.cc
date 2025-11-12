#include "log_manager.hh"
#include "status.hh"
#include "nvme_interface.hh"
#include <cstring>
#include <cstdint>
#include <iostream>
#include "def.hh"
#include <algorithm>
#include <memory>
#include <optional>

LogManager::LogManager(INVMEDriver& nvme)
    : next_lbn_(0), currenet_lbn_(0), page_offset_(0), byte_offset_(0) ,first_block_offset_(0),nvme_(nvme) {}

void LogManager::allocate_lbn() {
    char *buffer = (char*)calloc(sizeof(uint32_t), 1);
    if (!buffer) {
        std::cerr << "Buffer is null, cannot allocate LBN." << std::endl;
        return;
    }

    if (nvme_.nvme_allcate_lbn(buffer) == COMMAND_FAILED) {
        std::cerr << "Failed to allocate LBN." << std::endl;
        free(buffer);
        return;
    }

    uint32_t lbn = *(uint32_t *)(buffer);
    logRecordBlock_.push_back(lbn);
    currenet_lbn_ = next_lbn_;
    next_lbn_ = lbn;

    free(buffer);
}

void LogManager::flush_buffer()
{
    if (byte_offset_ == 0) return;             

    if (buffer_.size() < IMS_PAGE_SIZE) {
        buffer_.resize(IMS_PAGE_SIZE, '\0');
    }
    alignas(4096) static char aligned_page[IMS_PAGE_SIZE];
    std::memcpy(aligned_page, buffer_.data(), IMS_PAGE_SIZE);

    uint64_t lpn = static_cast<uint64_t>(LBN2LPN(currenet_lbn_) + page_offset_);
    // pr_info("Flushing buffer to LPN: %lu", lpn);
    // printf("[FLUSH] th=%lu currentLPN=%u\n",pthread_self(), lpn);
    if (nvme_.nvme_write_log(lpn, aligned_page) == COMMAND_FAILED) {
        std::cerr << "Failed to write log at LPN: " << lpn << '\n';
        return;
    }

    buffer_.clear();
    byte_offset_ = 0;

    if (++page_offset_ >= IMS_PAGE_NUM) {
        page_offset_ = 0;
        allocate_lbn();
    }
}



void LogManager::writeLog(const Record& log)
{   
    // log.Dump();
    const std::string encoded = log.Encode();
    uint32_t copied = 0;
    const uint32_t total = encoded.size();
    // pr_info("Writing log size: %lu", total);
    while (copied < total) {
        if (byte_offset_ == IMS_PAGE_SIZE) {
        // printf("[LOG] th=%lu byte_off=%u total=%u\n",pthread_self(), byte_offset_, total);
            flush_buffer();
        }

        uint32_t space = IMS_PAGE_SIZE - byte_offset_;
        uint32_t n     = std::min(space, total - copied);

        buffer_.append(encoded, copied, n);
        byte_offset_ += n;
        copied       += n;
    }
    if (byte_offset_ == IMS_PAGE_SIZE) {
        flush_buffer();
    }
}

uint32_t LogManager::findNextLPN(uint32_t lpn) const{
    uint32_t currentLBN = LPN2LBN(lpn);
    size_t pageOffset   = lpn - LBN2LPN(currentLBN);
    pr_info("Current LBN: %u, Page Offset: %zu", currentLBN, pageOffset);
    if (pageOffset + 1 >= IMS_PAGE_NUM) {
        auto blkIt = std::find(logRecordBlock_.begin(), logRecordBlock_.end(), currentLBN);
        if (blkIt == logRecordBlock_.end() || ++blkIt == logRecordBlock_.end()) {
            std::cerr << "[findNextLPN] No next block after LBN: " << currentLBN << '\n';
            return UINT32_MAX;
        }

        uint32_t nextLBN = *blkIt;
        return LBN2LPN(nextLBN);
    } else {
        return lpn + 1;
    }
}


constexpr size_t kHeader = sizeof(uint32_t) * 2;

std::optional<Record> LogManager::readLog(uint32_t lpn, uint32_t offset)
{
    auto readPage = [&](uint32_t lpn,char* buffer) -> bool{
        int err = nvme_.nvme_read_log(lpn,buffer);
        if(err == COMMAND_FAILED){
            pr_debug("NNMe read log is failed");
            return false;
        }
        return true;
    };
    std::string result;
    char *read_buffer = (char*)aligned_alloc(4096, IMS_PAGE_SIZE);
    uint32_t curLPN = lpn;
    uint32_t curOffset = offset;
    if (!read_buffer) {
        std::cerr << "Failed to allocate read buffer." << std::endl;
        return std::nullopt;
    }
    
    size_t firstPageByte = IMS_PAGE_SIZE - offset;

    if(lpn == getLPN()){
        result.append(buffer_.data() + curOffset,kHeader);
        curOffset = offset + kHeader;
    }
    else{
        if(!readPage(curLPN,read_buffer)){
            std::free(read_buffer);
            return std::nullopt;
        }
        if(firstPageByte >= kHeader ){
            result.append(read_buffer + offset,kHeader);
            curOffset = offset + kHeader;  
        }
        else{
            result.append(read_buffer + offset,firstPageByte);
            curLPN = findNextLPN(curLPN);
            if(curLPN == getLPN()){
                result.append(buffer_.data(), kHeader - firstPageByte);
            }
            else{
                if(!readPage(curLPN,read_buffer)){
                    std::free(read_buffer);
                    return std::nullopt;
                }
                result.append(read_buffer,kHeader - firstPageByte);
            }
            curOffset = kHeader - firstPageByte;
        }
    }

    uint32_t ikey_sz = 0;
    uint32_t val_sz = 0;
    memcpy(&ikey_sz,result.data(),sizeof(uint32_t));
    memcpy(&val_sz,result.data() + sizeof(uint32_t),sizeof(uint32_t));
    uint32_t blobSize = ikey_sz + val_sz;
    if (ikey_sz > 64) {
        pr_debug("Reading log at LPN: %u, Offset: %u, Blob Size: %u (Key size: %u,value size: %u)", lpn, offset, blobSize,ikey_sz,val_sz);
        free(read_buffer);
        return std::nullopt;   
    }
    uint32_t copied = 0;
    while (copied < blobSize) {
        if (curOffset == IMS_PAGE_SIZE && curLPN != getLPN()) {       
            curLPN = findNextLPN(curLPN);
            if(!readPage(curLPN, read_buffer)) {
                std::free(read_buffer);
                return std::nullopt;
            }
            curOffset = 0;
        }
        

        uint32_t room = IMS_PAGE_SIZE - curOffset;
        uint32_t n    = std::min(room, blobSize - copied);
        if (curLPN == getLPN())
        {
            result.append(buffer_.data() + curOffset, n);
        }
        else{
            result.append(read_buffer + curOffset, n);
        }
        curOffset += n;
        copied    += n;
    }

    Record record;
    record = Record::Decode(result);
    std::free(read_buffer);
    return record;
}


std::vector<Record> LogManager::readLogBlock(uint32_t lbn,
                                             uint32_t valid_offset,
                                             uint32_t &nextBlockValidOffset) {
    auto readBlock = [&](uint32_t lbn, char* buffer) -> bool {
        int err = nvme_.nvme_read_block(lbn, buffer);
        if (err == COMMAND_FAILED) {
            pr_debug("NVMe read log failed at LBN %u (err=%d)", lbn, err);
            return false;
        }
        return true;
    };

    std::vector<Record> results;

    if (lbn >= LBN_NUM) {
        pr_debug("LBN %u is out of range (LBN_NUM=%u)", lbn, LBN_NUM);
        nextBlockValidOffset = UINT32_MAX;
        return results;
    }
    if (valid_offset >= BLOCK_SIZE) {
        pr_debug("Valid offset %u is out of range (BLOCK_SIZE=%u)", valid_offset, BLOCK_SIZE);
        nextBlockValidOffset = UINT32_MAX;
        return results;
    }

    char* read_buffer = (char*)aligned_alloc(4096, BLOCK_SIZE);
    if (!read_buffer) {
        std::cerr << "Failed to allocate read buffer." << std::endl;
        nextBlockValidOffset = UINT32_MAX;
        return results;
    }

    if (!readBlock(lbn, read_buffer)) {
        pr_debug("Read block failed at LBN %u", lbn);
        nextBlockValidOffset = UINT32_MAX;
        free(read_buffer);
        return results;
    }

    uint32_t ikey_sz = 0;
    uint32_t val_sz  = 0;
    constexpr size_t kHeader = sizeof(uint32_t) * 2;

    size_t curOffset = valid_offset; 
    while (curOffset + kHeader <= BLOCK_SIZE) {
        // 讀 header
        std::memcpy(&ikey_sz, read_buffer + curOffset, sizeof(uint32_t));
        std::memcpy(&val_sz,  read_buffer + curOffset + sizeof(uint32_t), sizeof(uint32_t));

        if (ikey_sz != 64) {
            pr_debug("Invalid key size %u at LBN %u, Offset %zu", ikey_sz, lbn, curOffset);
            nextBlockValidOffset = UINT32_MAX;
            break;
        }

        const size_t rec_size = static_cast<size_t>(kHeader) +
                                static_cast<size_t>(ikey_sz) +
                                static_cast<size_t>(val_sz);

        if (curOffset + rec_size > BLOCK_SIZE) {
            pr_debug("Record exceeds block boundary at LBN %u, Offset %zu", lbn, curOffset);
            nextBlockValidOffset = UINT32_MAX;
            break;
        }

        std::string record_buf(read_buffer + curOffset, rec_size);
        Record record = Record::Decode(record_buf);
        results.push_back(std::move(record));

        curOffset += rec_size;
    }

    const size_t curBlockRemainder = BLOCK_SIZE - curOffset;

    const uint32_t baseLPN = LBN2LPN(lbn);

    const uint32_t curLPN  = baseLPN + static_cast<uint32_t>(curOffset / IMS_PAGE_SIZE);
    const uint32_t curOffsetInLPN = static_cast<uint32_t>(curOffset % IMS_PAGE_SIZE);   

    if (curBlockRemainder != 0) {
        auto rec = readLog(curLPN, curOffsetInLPN);
        if (rec.has_value()) {
            results.push_back(*rec);
            ikey_sz = rec->internal_key_size;
            val_sz  = rec->value_size;

            if (ikey_sz != 64) {
                nextBlockValidOffset = UINT32_MAX;
                pr_debug("Invalid key size %u at LPN %u, Offset %u", ikey_sz, curLPN, curOffsetInLPN);
            }

            
            constexpr size_t kHdr = sizeof(uint32_t) * 2;
            const size_t rec_size = static_cast<size_t>(kHdr) +
                                    static_cast<size_t>(ikey_sz) +
                                    static_cast<size_t>(val_sz);
            nextBlockValidOffset = static_cast<uint32_t>(rec_size - curBlockRemainder);
            
        } else {
            nextBlockValidOffset = UINT32_MAX;
            pr_debug("Failed to read cross-page log at LPN %u, Offset %u", curLPN, curOffsetInLPN);
        }
    }
    else{
        nextBlockValidOffset = 0;
    }

    free(read_buffer);
    return results;
}




void LogManager::getLPN(uint32_t& lpn, uint32_t& offset) const {
    lpn = LBN2LPN(currenet_lbn_) + page_offset_;
    offset = byte_offset_;
}
uint32_t LogManager::getLPN() const {
    return LBN2LPN(currenet_lbn_) + page_offset_;
}

void LogManager::clearLog() {
    logRecordBlock_.clear();
    currenet_lbn_ = 0;
    next_lbn_ = 1;
    page_offset_ = 0;
    byte_offset_ = 0;
}
bool LogManager::decode(const std::string& buf) {
    if (buf.size() % 4 != 0) return false;
    logRecordBlock_.clear();

    for (size_t i = 0; i < buf.size(); i += 4) {
        uint32_t val = 0;
        for (int j = 0; j < 4; ++j) {
            val |= static_cast<uint8_t>(buf[i + j]) << (j * 8);  // Little Endian
        }
        logRecordBlock_.push_back(val);
    }
    return true;
}



void LogManager::dump() const {
    std::cout << "========== LogManager Dump ==========\n";
    std::cout << "Current LBN           : " << currenet_lbn_ << "\n";
    std::cout << "Next LBN              : " << next_lbn_ << "\n";
    std::cout << "Page Offset           : " << page_offset_ << "\n";
    std::cout << "Byte Offset           : " << byte_offset_ << "\n";
    std::cout << "First Block Offset    : " << first_block_offset_ << "\n";
    std::cout << "Buffer Size           : " << buffer_.size() << " bytes\n";

    std::cout << "Log Record Blocks (" << logRecordBlock_.size() << " entries):\n";
    size_t count = 0;
    for (auto lbn : logRecordBlock_) {
        std::cout << "  [" << count++ << "] LBN = " << lbn << "\n";
    }

    std::cout << "======================================\n";
}

