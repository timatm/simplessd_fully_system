
#include "log_manager.hh"


void LOG_MANAGER::getLPN(uint32_t & current_lpn, uint32_t & byte_offset) const {
        current_lpn = LBN2LPN(currenet_lbn_) + page_offset_;
        byte_offset = byte_offset_;
}