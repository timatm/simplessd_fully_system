#include "log_manager.hh"
#include "nvme_interface.hh"
#include <cstring>
#include <cstdint>
#include <iostream>
#include "def.hh"
#include <algorithm>

LOG_MANAGER::LOG_MANAGER(NVMe& nvme)
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

void LOG_MANAGER::flush_buffer() {
    if (buffer_.size() != IMS_PAGE_SIZE) {
        std::cerr << "Buffer size mismatch! Actual: " << buffer_.size()
                  << ", Expected: " << IMS_PAGE_SIZE << std::endl;
        return;
    }

    uint64_t lpn = static_cast<uint64_t>(LBN2LPN(currenet_lbn_) + page_offset_);

    char* write_data = new char[buffer_.size()];
    std::memcpy(write_data, buffer_.data(), buffer_.size());

    int err = nvme_.nvme_write_log(lpn, write_data);
    if (err == COMMAND_FAILD) {
        std::cerr << "Failed to write log at LPN: " << lpn << std::endl;
        delete[] write_data;
        return;
    }

    delete[] write_data;
    buffer_.clear();

    if (++page_offset_ >= IMS_PAGE_NUM) {
        page_offset_ = 0;
        allocate_lbn();
    }

    byte_offset_ = 0;
}


void LOG_MANAGER::writeLog(const Record& log) {
    std::string encoded_log = log.Encode();
    uint32_t record_size = encoded_log.size();
    uint32_t copied = 0;

    while (copied < record_size) {
        uint32_t space_left = IMS_PAGE_SIZE - byte_offset_;
        uint32_t to_copy = std::min(space_left, record_size - copied);

        buffer_.append(encoded_log, copied, to_copy);
        byte_offset_ += to_copy;
        copied += to_copy;

        if (byte_offset_ == IMS_PAGE_SIZE) {
            flush_buffer();
        }
    }
}




Record* LOG_MANAGER::readLog(uint32_t lpn, uint32_t offset) const {
    auto it = std::find(logRecordBlock_.begin(), logRecordBlock_.end(),LPN2LBN(lpn));
    if( it == logRecordBlock_.end()) {
        std::cerr << "Log block not found for LBN: " << LPN2LBN(lpn) << std::endl;
        return nullptr;
    }
    uint32_t block_index = LPN2LBN(lpn);
    uint32_t page_index = lpn - LBN2LPN(block_index);
    std::string record_data;
    char* buffer = (char*)calloc(1, IMS_PAGE_SIZE);
    if (!buffer) return nullptr;

    uint32_t currentLPN = lpn;
    uint32_t currentOffset = offset;

    if (nvme_.nvme_read_log(currentLPN, buffer) == COMMAND_FAILD) {
        std::cerr << "Failed to read log at LPN: " << currentLPN << std::endl;
        free(buffer);
        return nullptr;
    }

    // decode head for value size and total record size
    std::string first_part(buffer + currentOffset, IMS_PAGE_SIZE - currentOffset);
    Record head = Record::Decode(first_part);

    uint32_t record_size = head.value_size + head.internal_key_size
                         + sizeof(head.internal_key_size) + sizeof(head.value_size);

    uint32_t first_chunk = std::min(IMS_PAGE_SIZE - currentOffset, record_size);
    record_data.append(buffer + currentOffset, first_chunk);

    uint32_t remaining = record_size > first_chunk ? (record_size - first_chunk) : 0;

    while (remaining > 0) {
        page_index++;
        currentLPN++;
        if(page_index >= IMS_PAGE_NUM) {
            if (std::next(it) != logRecordBlock_.end()) {
                block_index = *std::next(it);
            } else {
                std::cerr << "Reached end of log blocks while reading LBN" << std::endl;
                free(buffer);
                return nullptr;
            }
            page_index = 0;
            currentLPN = LBN2LPN(block_index);
        }
        if (nvme_.nvme_read_log(currentLPN, buffer) == COMMAND_FAILD) {
            std::cerr << "Failed to read log at LPN: " << currentLPN << std::endl;
            free(buffer);
            return nullptr;
        }
        uint32_t to_copy = std::min(remaining, (uint32_t)IMS_PAGE_SIZE);
        record_data.append(buffer, to_copy);
        remaining -= to_copy;
    }

    free(buffer);
    return new Record(Record::Decode(record_data));
}

void LOG_MANAGER::getLPN(uint32_t& lpn, uint32_t& offset) const {
    lpn = currenet_lbn_ * IMS_PAGE_NUM + page_offset_;
    offset = byte_offset_;
}

void LOG_MANAGER::ClearLog() {
    logRecordBlock_.clear();
    currenet_lbn_ = 0;
    next_lbn_ = 1;
    page_offset_ = 0;
    byte_offset_ = 0;
}
