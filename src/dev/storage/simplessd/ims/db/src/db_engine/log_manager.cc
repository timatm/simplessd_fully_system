#include "log_manager.hh"
#include "nvme_interface.hh"
#include <cstring>
#include <cstdint>
#include <iostream>
#include "def.hh"
#include <algorithm>
#include <memory>
#include <optional>
LOG_MANAGER::LOG_MANAGER(INVMEDriver& nvme)
    : next_lbn_(0), currenet_lbn_(0), page_offset_(0), byte_offset_(0) ,nvme_(nvme) {}

void LOG_MANAGER::allocate_lbn() {
    char *buffer = (char*)calloc(sizeof(uint32_t), 1);
    if (!buffer) {
        std::cerr << "Buffer is null, cannot allocate LBN." << std::endl;
        return;
    }

    if (nvme_.nvme_allcate_lbn(buffer) == COMMAND_FAILD) {
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

void LOG_MANAGER::flush_buffer()
{
    if (byte_offset_ == 0) return;             

    if (buffer_.size() < IMS_PAGE_SIZE) {
        buffer_.resize(IMS_PAGE_SIZE, '\0');
    }

    alignas(4096) static char aligned_page[IMS_PAGE_SIZE];
    std::memcpy(aligned_page, buffer_.data(), IMS_PAGE_SIZE);

    uint64_t lpn = static_cast<uint64_t>(LBN2LPN(currenet_lbn_) + page_offset_);
    pr_info("Flushing buffer to LPN: %lu", lpn);

    if (nvme_.nvme_write_log(lpn, aligned_page) == COMMAND_FAILD) {
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



void LOG_MANAGER::writeLog(const Record& log)
{
    const std::string encoded = log.Encode();
    uint32_t copied = 0;
    const uint32_t total = encoded.size();

    while (copied < total) {
        if (byte_offset_ == IMS_PAGE_SIZE) { 
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

uint32_t LOG_MANAGER::findNextLPN(uint32_t lpn) const{
    uint32_t currentLBN = LPN2LBN(lpn);
    size_t pageOffset   = lpn - LBN2LPN(currentLBN);

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


std::optional<Record> LOG_MANAGER::readLog(uint32_t lpn, uint32_t offset)
{
    auto readPage = [&](uint32_t lpn,char* buffer) -> bool{
        int err = nvme_.nvme_read_log(lpn,buffer);
        if(err == COMMAND_FAILD){
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
    uint32_t ikey_sz = 0;
    uint32_t val_sz = 0;
    size_t firstPageByte = IMS_PAGE_SIZE - offset;
    constexpr size_t kHeader = sizeof(uint32_t) * 2; 
    if(!readPage(curLPN,read_buffer)){
        std::free(read_buffer);
        return std::nullopt;
    }
    if(firstPageByte >= kHeader ){
        result.append(read_buffer+offset,kHeader);
        curOffset+= kHeader;
    }
    else{
        result.append(read_buffer+offset,firstPageByte);
        curLPN = findNextLPN(curLPN);
        if(!readPage(curLPN,read_buffer)){
            std::free(read_buffer);
            return std::nullopt;
        }
        result.append(read_buffer,kHeader-firstPageByte);
        curOffset = kHeader-firstPageByte;
    }
    memcpy(&ikey_sz,result.data(),sizeof(uint32_t));
    memcpy(&val_sz,result.data()+sizeof(uint32_t),sizeof(uint32_t));
    uint32_t blobSize = ikey_sz + val_sz;
    uint32_t copied = 0;
    while (copied < blobSize) {
        if (curOffset == IMS_PAGE_SIZE) {       
            curLPN = findNextLPN(curLPN);
            if(!readPage(curLPN, read_buffer)) {
                std::free(read_buffer);
                return std::nullopt;
            }
            curOffset = 0;
        }

        uint32_t room = IMS_PAGE_SIZE - curOffset;
        uint32_t n    = std::min(room, blobSize - copied);

        result.append(read_buffer + curOffset, n);

        curOffset += n;
        copied     += n;
    }

    Record record;
    record = Record::Decode(result);
    std::free(read_buffer);
    return record;
}



void LOG_MANAGER::getLPN(uint32_t& lpn, uint32_t& offset) const {
    lpn = LBN2LPN(currenet_lbn_) + page_offset_;
    offset = byte_offset_;
}

void LOG_MANAGER::clearLog() {
    logRecordBlock_.clear();
    currenet_lbn_ = 0;
    next_lbn_ = 1;
    page_offset_ = 0;
    byte_offset_ = 0;
}
bool LOG_MANAGER::decode(const std::string& buf) {
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



void LOG_MANAGER::dump() const {
    std::cout << "========== LOG_MANAGER Dump ==========\n";
    std::cout << "Current LBN     : " << currenet_lbn_ << "\n";
    std::cout << "Next LBN        : " << next_lbn_ << "\n";
    std::cout << "Page Offset     : " << page_offset_ << "\n";
    std::cout << "Byte Offset     : " << byte_offset_ << "\n";
    std::cout << "Buffer Size     : " << buffer_.size() << " bytes\n";

    std::cout << "Log Record Blocks (" << logRecordBlock_.size() << " entries):\n";
    size_t count = 0;
    for (auto lbn : logRecordBlock_) {
        std::cout << "  [" << count++ << "] LBN = " << lbn << "\n";
    }

    std::cout << "======================================\n";
}
