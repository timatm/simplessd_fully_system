#include "def.hh"
#include <iostream>
#include <iomanip>


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
    append_u32(byte_offset);
    append_u32(first_block_offset);
    append_str(log_list);
    append_str(node_list);

    return buf;
}




bool DB_INIT::decode(const std::string& buf, DB_INIT& out) {
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

    if (!read_u64(result.sstable_seq)) return false;
    if (!read_u64(result.global_seq))  return false;
    if (!read_u32(result.next_lbn))    return false;
    if (!read_u32(result.current_lbn)) return false;
    if (!read_u32(result.page_offset)) return false;
    if (!read_u32(result.byte_offset)) return false;
    if (!read_u32(result.first_block_offset)) return false;

    if (!read_str(result.log_list))    return false;
    if (!read_str(result.node_list))   return false;
    out = result;
    return true;
}


void DB_INIT::dump() const {
    std::cout << "========== DB_INIT Dump ==========\n";
    std::cout << "SSTable Seq           : " << sstable_seq << "\n";
    std::cout << "Global Seq            : " << global_seq << "\n";
    std::cout << "Current LBN           : " << current_lbn << "\n";
    std::cout << "Next LBN              : " << next_lbn << "\n";
    std::cout << "Page Offset           : " << page_offset << "\n";
    std::cout << "Byte Offset           : " << byte_offset << "\n";
    std::cout << "First Block Offset    : " << page_offset << "\n";

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


namespace {

constexpr std::size_t kSSTNameLen = 35;

inline void PutU32(std::string& dst, uint32_t v) {
    char b[4];
    b[0] = static_cast<char>( v        & 0xFF);
    b[1] = static_cast<char>((v >> 8)  & 0xFF);
    b[2] = static_cast<char>((v >> 16) & 0xFF);
    b[3] = static_cast<char>((v >> 24) & 0xFF);
    dst.append(b, 4);
}

inline bool GetU32(const std::string& src, size_t& off, uint32_t& out) {
    if (off + 4 > src.size()) return false;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(src.data() + off);
    out =  static_cast<uint32_t>(p[0])
        | (static_cast<uint32_t>(p[1]) << 8)
        | (static_cast<uint32_t>(p[2]) << 16)
        | (static_cast<uint32_t>(p[3]) << 24);
    off += 4;
    return true;
}

inline bool GetBytes(const std::string& src, size_t& off, uint32_t len, std::string& out) {
    if (off + len > src.size()) return false;
    out.assign(src.data() + off, len);
    off += len;
    return true;
}


inline void PutFixedExact(std::string& dst, std::string_view src, std::size_t fixed_len) {
    if (src.size() != fixed_len) {
        throw std::length_error("sstable_name must be exactly 36 bytes");
    }
    dst.append(src.data(), fixed_len);
}


inline bool GetFixed(const std::string& src, size_t& off, std::size_t fixed_len, std::string& out) {
    if (off + fixed_len > src.size()) return false;
    out.assign(src.data() + off, fixed_len);
    off += fixed_len;
    return true;
}

constexpr uint32_t kMagic_D = 0x444B5053; // 'S','P','K','D'（小端）
constexpr uint32_t kMagic_H = 0x484B5053; // 'S','P','K','H'（小端）

inline void EnsureU32Len(std::size_t n, const char* what) {
    if (n > std::numeric_limits<uint32_t>::max()) {
        throw std::length_error(std::string(what) + " too large for u32 length field");
    }
}

} // namespace


// ======= SearchPatternD =======
// 编码格式：
//   u32 name_len
//   [name_len bytes] sstable_name
//   u32 slot_index
// 编码： [36B] sstable_name + u32 slot_index
std::string SearchPatternD::encode() const {
    std::string out;
    out.reserve(kSSTNameLen + 4);
    PutFixedExact(out, sstable_name, kSSTNameLen); // 不等 36B 直接抛异常
    PutU32(out, slot_index);
    return out;
}

bool SearchPatternD::decode(const std::string& buf, SearchPatternD& out) {
    SearchPatternD pat{};
    size_t off = 0;

    if (!GetFixed(buf, off, kSSTNameLen, pat.sstable_name)) return false;

    uint32_t slot = 0;
    if (!GetU32(buf, off, slot)) return false;
    pat.slot_index = slot;
    out = pat;
    return true; // 允许 slice 有尾随字节
}


void SearchPatternD::dump() const {
    std::cout << "SearchPatternD { name=\"" << sstable_name
              << "\", slot_index=" << slot_index << " }\n";
}

// ======= SearchPackageD =======
// 编码格式：
//   u32 magic (==kMagic_D)
//   u32 pattern_num (= searchPatterns.size())
//   u32 key_len
//   [key_len bytes] search_key
//   repeat pattern_num times:
//     u32 pat_len
//     [pat_len bytes] SearchPatternD::encode()
std::string SearchPackageD::encode() const {
    EnsureU32Len(search_key.size(), "search_key");
    auto pat_num = static_cast<uint32_t>(searchPatterns.size());

    // 预估长度（减少realloc）
    std::size_t hint = 12 + search_key.size();
    for (const auto& p : searchPatterns) {
        auto enc = p.encode();
        EnsureU32Len(enc.size(), "SearchPatternD");
        hint += 4 + enc.size();
    }

    std::string out;
    out.reserve(hint);

    PutU32(out, kMagic_D);
    PutU32(out, pat_num);
    PutU32(out, static_cast<uint32_t>(search_key.size()));
    out.append(search_key);

    for (const auto& p : searchPatterns) {
        auto enc = p.encode();
        PutU32(out, static_cast<uint32_t>(enc.size()));
        out.append(enc);
    }
    return out;
}

bool SearchPackageD::decode(const std::string& buf, SearchPackageD& out) {
    SearchPackageD pkg{};
    size_t off = 0;

    uint32_t magic = 0, pattern_num = 0, key_len = 0;
    if (!GetU32(buf, off, magic)) return false;
    if (magic != kMagic_D) return false;
    pkg.header.magic = magic;

    if (!GetU32(buf, off, pattern_num)) return false;
    pkg.header.pattern_num = pattern_num;

    if (!GetU32(buf, off, key_len)) return false;
    if (!GetBytes(buf, off, key_len, pkg.search_key)) return false;

    pkg.searchPatterns.clear();
    pkg.searchPatterns.reserve(pattern_num);

    for (uint32_t i = 0; i < pattern_num; ++i) {
        uint32_t pat_len = 0;
        if (!GetU32(buf, off, pat_len)) return false;
        if (off + pat_len > buf.size()) return false;

        std::string slice(buf.data() + off, pat_len);
        off += pat_len;

        SearchPatternD pat;
        if( SearchPatternD::decode(slice,pat) == false){
            return false;
        }
        pkg.searchPatterns.push_back(std::move(pat));
    }

    // 严格模式：必须刚好消费完
    if (off != buf.size()) return false;
    out = pkg;
    return true;
}

void SearchPackageD::dump() const {
    std::cout << "SearchPackageD {\n";
    std::cout << "  magic       : 0x" << std::hex << header.magic << std::dec << "\n";
    std::cout << "  pattern_num : " << header.pattern_num
              << " (vec.size=" << searchPatterns.size() << ")\n";
    std::cout << "  search_key  : \"" << search_key << "\" (len=" << search_key.size() << ")\n";
    for (size_t i = 0; i < searchPatterns.size(); ++i) {
        std::cout << "  [pat#" << i << "] ";
        searchPatterns[i].dump();
    }
    std::cout << "}\n";
}

// ======= SearchPatternH =======
// 编码格式：
//   u32 name_len
//   [name_len bytes] sstable_name
//   u32 pattern_len
//   [pattern_len bytes] searh_pattern
// 编码： [36B] sstable_name + u32 len + [len] searh_pattern
std::string SearchPatternH::encode() const {
    std::string out;
    out.reserve(kSSTNameLen + 4 + search_pattern.size());

    PutFixedExact(out, sstable_name, kSSTNameLen); // 严格 36B
    EnsureU32Len(search_pattern.size(), "search_pattern");
    PutU32(out, static_cast<uint32_t>(search_pattern.size()));
    out.append(search_pattern);
    return out;
}

bool SearchPatternH::decode(const std::string& buf, SearchPatternH& out) {
    SearchPatternH pat{};
    size_t off = 0;

    if (!GetFixed(buf, off, kSSTNameLen, pat.sstable_name)) return false;
    // 不做裁剪

    uint32_t patt_len = 0;
    if (!GetU32(buf, off, patt_len)) return false;
    if (!GetBytes(buf, off, patt_len, pat.search_pattern)) return false;
    out = pat;
    return true;
}


void SearchPatternH::dump() const {
    std::cout << "SearchPatternH { name=\"" << sstable_name
              << "\", searh_pattern_len=" << search_pattern.size() << " }\n";
}

// ======= SearchPackageH =======
// 编码格式：
//   u32 magic (==kMagic_H)
//   u32 pattern_num (= searchPatterns.size())
//   u32 key_len
//   [key_len bytes] search_key
//   repeat pattern_num times:
//     u32 pat_len
//     [pat_len bytes] SearchPatternH::encode()
std::string SearchPackageH::encode() const {
    EnsureU32Len(search_key.size(), "search_key");
    auto pat_num = static_cast<uint32_t>(searchPatterns.size());

    std::size_t hint = 12 + search_key.size();
    for (const auto& p : searchPatterns) {
        auto enc = p.encode();
        EnsureU32Len(enc.size(), "SearchPatternH");
        hint += 4 + enc.size();
    }

    std::string out;
    out.reserve(hint);

    PutU32(out, kMagic_H);
    PutU32(out, pat_num);
    PutU32(out, static_cast<uint32_t>(search_key.size()));
    out.append(search_key);

    for (const auto& p : searchPatterns) {
        auto enc = p.encode();
        PutU32(out, static_cast<uint32_t>(enc.size()));
        out.append(enc);
    }
    return out;
}

bool SearchPackageH::decode(const std::string& buf, SearchPackageH& out) {
    SearchPackageH pkg{};
    size_t off = 0;

    uint32_t magic = 0, pattern_num = 0, key_len = 0;
    if (!GetU32(buf, off, magic)) return false;
    if (magic != kMagic_H) return false;
    pkg.header.magic = magic;

    if (!GetU32(buf, off, pattern_num)) return false;
    pkg.header.pattern_num = pattern_num;

    if (!GetU32(buf, off, key_len)) return false;
    if (!GetBytes(buf, off, key_len, pkg.search_key)) return false;

    pkg.searchPatterns.clear();
    pkg.searchPatterns.reserve(pattern_num);

    for (uint32_t i = 0; i < pattern_num; ++i) {
        uint32_t pat_len = 0;
        if (!GetU32(buf, off, pat_len)) return false;
        if (off + pat_len > buf.size()) return false;

        std::string slice(buf.data() + off, pat_len);
        off += pat_len;
        SearchPatternH pat;
        if(SearchPatternH::decode(slice,pat) == false){
            return false;
        }
        pkg.searchPatterns.push_back(std::move(pat));
    }

    if (off != buf.size()) return false;
    out = pkg;
    return true;
}

void SearchPackageH::dump() const {
    std::cout << "SearchPackageH {\n";
    std::cout << "  magic       : 0x" << std::hex << header.magic << std::dec << "\n";
    std::cout << "  pattern_num : " << header.pattern_num
              << " (vec.size=" << searchPatterns.size() << ")\n";
    std::cout << "  search_key  : \"" << search_key << "\" (len=" << search_key.size() << ")\n";
    for (size_t i = 0; i < searchPatterns.size(); ++i) {
        std::cout << "  [pat#" << i << "] ";
        searchPatterns[i].dump();
    }
    std::cout << "}\n";
}