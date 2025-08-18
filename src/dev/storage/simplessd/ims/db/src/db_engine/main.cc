#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include "../db_nvme/../db_nvme/nvme_interface.hh"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "main.hh"
#include <iomanip>   
#include <cstdint>    
#include "db_api.hh"
// static void* allocateAligned(size_t size) {
//     void* ptr = nullptr;
//     if (posix_memalign(&ptr, 4096, size) != 0 || ptr == nullptr) {
//         throw std::bad_alloc();
//     }
//     std::memset(ptr, 0, size);
//     return ptr;
// }

// main.cc – smoke test for SstableIterator
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <optional>
#include <iostream>

// === 依你的專案調整這些 include ===
#include "sstable_mgr.hh"      // 內含 SstableIterator / SstableManager（或等價標頭）
#include "internal_key.hh"     // InternalKey / comparator
#include "status.hh"

// 小工具：把 InternalKey bytes 轉成可讀字串（依你實作調整）
static std::string key_to_str(std::string_view ik_bytes) {
    // 若你有 InternalKey::Decode(std::string_view)
    InternalKey ik = InternalKey::Decode(ik_bytes.data());
    return ik.UserKey();  // 你已有的 API
}

// 列印一段 range（前 N 筆）
static void dump_first_n(SstableIterator& it, int n, const char* title) {
    std::cout << "== " << title << " ==" << std::endl;
    int cnt = 0;
    for (it.SeekToFirst(); it.Valid() && cnt < n; it.Next(), ++cnt) {
        auto ksv = it.key();
        std::string ks = key_to_str(ksv);
        std::string v;
        Status s = it.ReadValue(v);   // 若該 iterator 支援 value
        if (!s.ok()) v = "<ReadValue failed>";
        std::cout << ks << " => " << v << "\n";
    }
    std::cout << "--------------\n";
}

// 逆向列印最後 N 筆
static void dump_last_n(SstableIterator& it, int n, const char* title) {
    std::cout << "== " << title << " ==" << std::endl;
    int cnt = 0;
    for (it.SeekToLast(); it.Valid() && cnt < n; it.Prev(), ++cnt) {
        auto ksv = it.key();
        std::string ks = key_to_str(ksv);
        std::string v;
        Status s = it.ReadValue(v);
        if (!s.ok()) v = "<ReadValue failed>";
        std::cout << ks << " => " << v << "\n";
    }
    std::cout << "--------------\n";
}

// 驗證 Seek：給定目標 key（user_key 字串），構造 InternalKey 目標並 Seek
static void test_seek(SstableIterator& it, const std::string& user_key, const char* tag) {
    // 依你的 InternalKey 編碼，構造「查找用」internal key（通常 seq=最大、type=Value）
    InternalKey target(user_key);
    target.info.seq = UINT32_MAX;  // 或你專案定義的「最大序號」
    target.info.type = /*kTypeValue*/ 1;  // 視你專案定義

    // 假設有 Encode() 回傳 bytes（string 或 string_view）
    std::string target_bytes = std::string(target.Encode());
    it.Seek(std::string_view(target_bytes.data(), target_bytes.size()));
    std::cout << "[Seek " << tag << " \"" << user_key << "\"] => ";
    if (it.Valid()) {
        std::string ks = key_to_str(it.key());
        std::string v;
        Status s = it.ReadValue(v);
        if (!s.ok()) v = "<ReadValue failed>";
        std::cout << ks << " | " << v << "\n";
    } else {
        std::cout << "<Invalid>\n";
    }
}

// 針對一個檔案（代表一張 SSTable）跑一輪完整 smoke test
static void run_table_test(SstableManager* smgr,
                           LOG_MANAGER*    lmgr,
                           const InternalKeyComparator& icmp,
                           const std::string& filename,
                           PackingType type,
                           const char* title)
{
    SstableIterator it(smgr, lmgr, &icmp, filename, type);

    Status s = it.Init();
    if (!s.ok()) {
        std::cerr << "Init failed for " << filename << ": " << s.ToString() << "\n";
        return;
    }

    // 基本遍歷
    dump_first_n(it, 10, (std::string(title) + " | first 10").c_str());
    dump_last_n(it, 10,  (std::string(title) + " | last 10").c_str());

    // 多組 Seek 測試（你可依你的資料集調整鍵值）
    // test_seek(it, "key0003", "exact");
    // test_seek(it, "key00033", "ceil");
    // test_seek(it, "zzz", "beyond-max");
}



int main() {
    API db;
    Status err = db.open();
    if(err.ok()) {
        // db.dump_all();
        std::cout << "Database opened successfully.\n";
    } else {
        std::cerr << "Failed to open database: " << err.ToString() << "\n";
        return -1;
    }
    
    for (int i = 0; i < 1024; ++i) {
        std::string key = "key" + std::to_string(i);
        std::string value = "value" + std::to_string(i);
        Status s = db.put(key, value);
        if (!s.ok()) {
            std::cerr << "Put failed at index " << i << " with key: " << key << "\n";
        }
    }
    db.getSSTable()->waitAllTasksDone();
    // db.getLogManager()->flush_buffer();
    // db.nvme_->nvme_dump_ims();


    // std::cout << "Inserted 129 key-value pairs successfully.\n";
    // db.getSSTable()->waitAllTasksDone();
    db.dump_memtable();
    db.dump_lsmtree();
    InternalKeyComparator icmp;
    // char * buffer = (char *)allocateAligned(BLOCK_SIZE);
    run_table_test(db.getSSTable(), db.getLogManager(), icmp, "00000000000000000000000000000000000",   PackingType::kKeyPerPage, "KeyPerPage");


    // std::string search_value;
    // std::optional<Record> result = db.getLogManager()->readLog(640,0);
    // if(result.has_value()){
    //     result->Dump();
    // } else {
    //     std::cout << "No record found at LPN 640, offset 0\n";
    // }
    // db.get("key20", search_value);
    // std::cout << search_value << std::endl;


   // // free(buffer);
    // return 0;
 
}



// int main() {
//     API db;
//     Status err = db.open();
//     if(err.ok()) {
//         db.dump_all();
//         std::cout << "Database opened successfully.\n";
//     } else {
//         std::cerr << "Failed to open database: " << err.ToString() << "\n";
//         return -1;
//     }
    
//     for (int i = 0; i < 700; ++i) {
//         std::string key = "key" + std::to_string(i);
//         std::string value = "value" + std::to_string(i);
//         Status s = db.put(key, value);
//         if (!s.ok()) {
//             std::cerr << "Put failed at index " << i << " with key: " << key << "\n";
//         }
//     }
   
//     db.getSSTable()->waitAllTasksDone();
//     // db.getLogManager()->flush_buffer();


//     std::string search_value;
//     db.get("key500", search_value);
//     // std::optional<Record> result = db.getLogManager()->readLog(640,0);
//     // if(result.has_value()){
//     //     result->Dump();
//     // } else {
//     //     std::cout << "No record found at LPN 640, offset 0\n";
//     // }
//     std::cout << "Search result for key0: " << search_value << std::endl;
 
// }

