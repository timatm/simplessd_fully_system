#include "def.hh"
#include <iostream>
#include <iomanip>
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

static inline void append_u32_le(std::string& out, uint32_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
    out.push_back(static_cast<char>((v >> 16) & 0xFF));
    out.push_back(static_cast<char>((v >> 24) & 0xFF));
}

static inline uint32_t load_u32_le(const char* p) {
    return  (static_cast<uint32_t>(static_cast<unsigned char>(p[0]))      ) |
           ((static_cast<uint32_t>(static_cast<unsigned char>(p[1])) << 8 )) |
           ((static_cast<uint32_t>(static_cast<unsigned char>(p[2])) << 16)) |
           ((static_cast<uint32_t>(static_cast<unsigned char>(p[3])) << 24));
}

std::string SearchPattern::encode() const {
    if (sstable_name.size() != 36) return {};
    std::string buf;
    buf.reserve(40);
    buf.append(sstable_name);
    append_u32_le(buf, slot_index);
    return buf;
}

std::optional<SearchPattern> SearchPattern::decode(const std::string& buf) {
    constexpr size_t kNameLen = 36;
    constexpr size_t kTotal   = kNameLen + 4;
    if (buf.size() != kTotal) return std::nullopt;

    SearchPattern sp;
    sp.sstable_name.assign(buf.data(), kNameLen);
    sp.slot_index = load_u32_le(buf.data() + kNameLen);
    return sp;
}

std::string SearchPackage::encode() const {
    constexpr uint32_t kMagic = 0x31504753u; // 'S','G','P','1' (LE)
    const uint32_t count = static_cast<uint32_t>(searchPatterns.size());

    const size_t kPatternSize = 40;
    std::string buf;
    buf.reserve(8 + static_cast<size_t>(count) * kPatternSize);

    // header
    append_u32_le(buf, kMagic);
    append_u32_le(buf, count);

    // body
    for (const auto& p : searchPatterns) {
        auto encoded = p.encode();
        if (encoded.size() != kPatternSize) return {};
        buf.append(encoded.data(), encoded.size());
    }
    return buf;
}

std::optional<SearchPackage> SearchPackage::decode(const std::string& buf){
    constexpr uint32_t kMagic = 0x31504753u;
    constexpr size_t kHeader = 8;
    constexpr size_t kPatternSize = 40;

    if (buf.size() < kHeader) return std::nullopt;

    size_t offset = 0;
    const uint32_t magic = load_u32_le(buf.data() + offset); offset += 4;
    const uint32_t count = load_u32_le(buf.data() + offset); offset += 4;

    if (magic != kMagic) return std::nullopt;

    const size_t body = buf.size() - offset;
    if (body / kPatternSize < count) return std::nullopt;

    SearchPackage spkg;
    spkg.header.magic = magic;
    spkg.header.pattern_num = count;

    spkg.searchPatterns.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        auto pattern_buf = buf.substr(offset, kPatternSize);
        auto pattern = SearchPattern::decode(pattern_buf);
        if (!pattern) return std::nullopt;
        spkg.searchPatterns.push_back(*pattern);
        offset += kPatternSize;
    }

    return spkg;
}



void SearchPattern::dump() const {
    std::cout << "SearchPattern {\n"
              << "  sstable_name : \"" << sstable_name << "\"\n"
              << "  slot_index   : " << slot_index << "\n"
              << "}\n";
}


void SearchPackage::dump() const {
    std::cout << "SearchPackage {\n"
              << "  header.magic       : 0x" 
              << std::hex << std::uppercase << header.magic 
              << std::dec << "\n"
              << "  header.pattern_num : " << header.pattern_num << "\n";

    if (searchPatterns.empty()) {
        std::cout << "  searchPatterns     : []\n";
    } else {
        std::cout << "  searchPatterns [\n";
        for (size_t i = 0; i < searchPatterns.size(); ++i) {
            std::cout << "    [" << i << "] ";
            searchPatterns[i].dump(); // 呼叫 SearchPattern 的 dump()
        }
        std::cout << "  ]\n";
    }
    std::cout << "}\n";
}
