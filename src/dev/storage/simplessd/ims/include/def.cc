#include "def.hh"
std::string DB_INIT::encode() {
    std::string buf;
    auto append_u32 = [&](uint32_t val) {
        for (int i = 0; i < 4; ++i) {
            buf += static_cast<char>((val >> (i * 8)) & 0xFF);
        }
    };
    auto append_u64 = [&](uint64_t val) {
        for (int i = 0; i < 8; ++i) {
            buf += static_cast<char>((val >> (i * 8)) & 0xFF);
        }
    };
    auto append_str = [&](const std::string& str) {
        append_u32(static_cast<uint32_t>(str.size()));
        buf.append(str);
    };
    append_u64(sstable_seq);
    append_u64(global_seq);
    append_u32(next_lbn);
    append_u32(current_lbn);
    append_u32(page_offset);
    append_str(log_list);
    append_str(node_list);

    return buf;
}




std::optional<DB_INIT> DB_INIT::decode(const std::string& buf) {
    size_t offset = 0;

    auto read_u32 = [&](uint32_t& val) -> bool {
        if (offset + 4 > buf.size()) return false;
        val = 0;
        for (int i = 0; i < 4; ++i) {
            val |= static_cast<uint32_t>(static_cast<uint8_t>(buf[offset++])) << (i * 8);
        }
        return true;
    };

    auto read_u64 = [&](uint64_t& val) -> bool {
        if (offset + 8 > buf.size()) return false;
        val = 0;
        for (int i = 0; i < 8; ++i) {
            val |= static_cast<uint64_t>(static_cast<uint8_t>(buf[offset++])) << (i * 8);
        }
        return true;
    };

    auto read_str = [&](std::string& out) -> bool {
        uint32_t len = 0;
        if (!read_u32(len)) return false;
        if (offset + len > buf.size()) return false;
        out.assign(buf.data() + offset, len);
        offset += len;
        return true;
    };

    DB_INIT result;

    if (!read_u64(result.sstable_seq)) return std::nullopt;
    if (!read_u64(result.global_seq))  return std::nullopt;
    if (!read_u32(result.next_lbn))    return std::nullopt;
    if (!read_u32(result.current_lbn)) return std::nullopt;
    if (!read_u32(result.page_offset)) return std::nullopt;

    if (!read_str(result.log_list))    return std::nullopt;
    if (!read_str(result.node_list))   return std::nullopt;

    return result;
}


void DB_INIT::dump() const {
    std::cout << "========== DB_INIT Dump ==========\n";
    std::cout << "SSTable Seq   : " << sstable_seq << "\n";
    std::cout << "Global Seq    : " << global_seq << "\n";
    std::cout << "Current LBN   : " << current_lbn << "\n";
    std::cout << "Next LBN      : " << next_lbn << "\n";
    std::cout << "Page Offset   : " << page_offset << "\n";

    std::cout << "Log List Size : " << log_list.size() << " bytes\n";
    if (!log_list.empty()) {
        std::cout << "Log List Preview: " 
                  << log_list.substr(0, std::min<size_t>(64, log_list.size())) 
                  << (log_list.size() > 64 ? "..." : "") << "\n";
    }

    std::cout << "Node List Size: " << node_list.size() << " bytes\n";
    if (!node_list.empty()) {
        std::cout << "Node List Preview: " 
                  << node_list.substr(0, std::min<size_t>(64, node_list.size())) 
                  << (node_list.size() > 64 ? "..." : "") << "\n";
    }

    std::cout << "==================================\n";
}
