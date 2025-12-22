#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <stdexcept>

#include "def.hh"          // 需要 IMS_PAGE_SIZE / IMS_PAGE_NUM / BLOCK_SIZE / SLOT_SIZE
#include "internal_key.hh" // 需要 InternalKey

// ---------------------------
// Format: Index + Bloom + Data (Version B)
// ---------------------------
// [Meta Region: meta_pages * IMS_PAGE_SIZE bytes]
//   Page0:
//     SuperBlock (固定 64B, offset=0)
//     Index entries (每個 data page 1 entry, 固定 48B/entry)
//     Bloom bitset (剩下空間，從 Page0 開始，可延伸到 Page1..meta_pages-1)
// [Data Region]
//   從 data_off (= meta_pages*IMS_PAGE_SIZE) 開始
//   依序放 InternalKey slots（每個 slot 64B）
//   slot layout: row-major (連續擺放)

namespace sst_v2 {

// 你可以把這個 format id 固定一個值（跟 PackingType 分開也可以）
enum : uint8_t {
    kFormatIdxBloomData = 1,
};

// 對齊工具（Bloom 開始偏移做 8 bytes 對齊，方便之後擴充/計算）
static inline uint32_t AlignUp(uint32_t x, uint32_t a) {
    return (x + a - 1) / a * a;
}

#pragma pack(push, 1)

// -------- SuperBlock (固定 64 bytes) --------
struct SSTableSuperBlockV1 {
    char     magic[4];        // "SSTB"
    uint16_t version;         // 1
    uint8_t  format;          // kFormatIdxBloomData
    uint8_t  meta_pages;      // meta region 佔用幾個 page（建議 >= 2）

    uint32_t page_size;       // IMS_PAGE_SIZE
    uint32_t block_size;      // BLOCK_SIZE

    uint32_t index_off;       // index 起始 offset（通常 = 64）
    uint32_t index_bytes;     // index 總 bytes (= index_entry_count * 48)

    uint32_t bloom_off;       // bloom 起始 offset（index 後對齊）
    uint32_t bloom_bytes;     // bloom bitset bytes（吃掉 meta region 剩餘空間）

    uint32_t data_off;        // data 起始 offset (= meta_pages * page_size)
    uint32_t entry_count;     // data region 實際寫入的 InternalKey 數量

    uint16_t index_entry_count; // data pages used（= ceil(entry_count / slots_per_page)）
    uint8_t  bloom_k;           // hash functions 數量
    uint8_t  reserved0;

    uint32_t crc32;           // 可先填 0（baseline 可不做）
    uint16_t filter_bytes_per_page;
    uint16_t reserved1;
    uint8_t  reserved[12];    // 預留擴充
};
static_assert(sizeof(SSTableSuperBlockV1) == 64, "SuperBlock must be 64 bytes");

// -------- IndexEntry (固定 48 bytes / entry) --------
// 用「每個 data page 的 max user-key」當索引 key
struct SSTableIndexEntryV1 {
    uint8_t  key_size;        // max user-key 的長度 (<=40)
    uint8_t  key[40];         // max user-key bytes，剩餘補 0
    uint16_t page_id;         // data region 的相對 page id (0..)
    uint16_t valid_slots;     // 該頁有效 slots 數（最後一頁可能 < slots_per_page）
    uint8_t  reserved[3];     // 補滿 48
};
static_assert(sizeof(SSTableIndexEntryV1) == 48, "IndexEntry must be 48 bytes");

#pragma pack(pop)

// ---- 一些格式檢查 helper（讀取時用，打包也可用）----
static inline bool CheckSuperBlock(const SSTableSuperBlockV1& sb) {
    if (std::memcmp(sb.magic, "SSTB", 4) != 0) return false;
    if (sb.version != 1) return false;
    if (sb.page_size != IMS_PAGE_SIZE) return false;
    if (sb.block_size != BLOCK_SIZE) return false;
    if (sb.data_off >= sb.block_size) return false;
    if (sb.bloom_off >= sb.block_size) return false;
    if (sb.index_off >= sb.block_size) return false;
    return true;
}

} // namespace sst_v2
