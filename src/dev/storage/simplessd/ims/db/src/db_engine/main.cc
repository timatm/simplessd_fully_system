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
    
//     for (int i = 0; i < 1024; ++i) {
//         std::string key = "key" + std::to_string(i);
//         std::string value = "value" + std::to_string(i);
//         Status s = db.put(key, value);
//         if (!s.ok()) {
//             std::cerr << "Put failed at index " << i << " with key: " << key << "\n";
//         }
//     }
//     db.getSSTable()->waitAllTasksDone();
//     db.getLogManager()->flush_buffer();
//     // db.nvme_->nvme_dump_ims();
//     // for (int i = 0; i < 129; ++i) {
//     //     std::string key = "key2";
//     //     std::string value = "value" + std::to_string(i);
//     //     Status s = db.put(key, value);
//     //     if (!s.ok()) {
//     //         std::cerr << "Put failed at index " << i << " with key: " << key << "\n";
//     //     }
//     // }



//     // std::cout << "Inserted 129 key-value pairs successfully.\n";
//     // db.getSSTable()->waitAllTasksDone();
//     // db.dump_memtable();
//     // db.nvme_->nvme_dump_ims();


//     // db.dump_lsmtree();
//     // char * buffer = (char *)allocateAligned(BLOCK_SIZE);



//     std::string search_value;
//     std::optional<Record> result = db.getLogManager()->readLog(640,0);
//     if(result.has_value()){
//         result->Dump();
//     } else {
//         std::cout << "No record found at LPN 640, offset 0\n";
//     }
//     // db.get("key20", search_value);
//     // std::cout << search_value << std::endl;


//    // // free(buffer);
//     // return 0;
 
// }



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


#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <cassert>

// ======= 假设你已有这些头文件/类型 =======
// 实际请替换为你的工程内的头档
#include "memtable.hh"        // 含 MemTable 定义
#include "skiplist.hh"        // 含 SkipList<Record, RecordComparator>
#include "record.hh"          // 含 Record 定义
#include "slice.hh"           // 如果 InternalKey / Slice 需要
#include "status.hh"          // Status
#include "db_api.hh"          // 如果有需要
// ==========================================

// 用来打印一条 Record（依你的 Record/Key API 调整）
static void PrintKV(const std::string_view internal_user_key,
                    const std::string_view value) {
    std::cout << "  key=" << internal_user_key << "  value=" << value << "\n";
}

// 简单辅助：构造 InternalKey 与 Record（依你的构造函数调整）
// 在 main() 一开始建立一个池，持久保存 key 的 storage


int main() {
    std::cout << "=== MemTable / MemTableIterator 功能測試 ===\n";

    // 1) 建立 MemTable，先測空表的 iterator 行為
    MemTable mt;

    // aliasing shared_ptr：不擁有，僅包裝 MemTable 內部的 skiplist_
    // 注意：GetSkipList() 回的是 const&，Iterator 通常需要非 const
    // 這裡做 const_cast 僅作測試用途
    auto* raw_ptr = const_cast<SkipList<Record, RecordComparator>*>(&mt.GetSkipList());
    std::shared_ptr<SkipList<Record, RecordComparator>> list_sp(raw_ptr, [](auto*){/* no-op */});

    // 你的比較器目前沒有被 iterator 使用，但還是傳一個指標（可為 nullptr）
    InternalKeyComparator icmp;  // 假設有預設建構；若沒有，傳 nullptr 也行
    MemTableIterator it(list_sp, &icmp);

    // 空表測試
    std::cout << "\n[空表測試]\n";
    std::cout << "Valid() 初始 = " << (it.Valid() ? "true" : "false") << "\n";
    it.SeekToFirst();
    std::cout << "SeekToFirst -> Valid() = " << (it.Valid() ? "true" : "false") << "\n";
    it.SeekToLast();
    std::cout << "SeekToLast  -> Valid() = " << (it.Valid() ? "true" : "false") << "\n";
    it.Seek("k10");
    std::cout << "Seek('k10') -> Valid() = " << (it.Valid() ? "true" : "false") << "\n";

    // 2) 插入一些資料
    std::cout << "\n[插入資料]\n";
    // std::vector<std::pair<std::string,std::string>> kvs = {
    //     {"k01","v01"}, {"k03","v03"}, {"k02","v02"},
    //     {"k10","v10"}, {"k07","v07"}, {"k20","v20"},
    // };
    Record ik1(InternalKey("k01",1,ValueType::kTypeValue));
    Record ik2(InternalKey("k03",2,ValueType::kTypeValue));
    Record ik3(InternalKey("k02",3,ValueType::kTypeValue));
    Record ik4(InternalKey("k10",4,ValueType::kTypeValue));
    Record ik5(InternalKey("k07",5,ValueType::kTypeValue));
    Record ik6(InternalKey("k20",6,ValueType::kTypeValue));
    ik1.value = "v01";
    ik2.value = "v03";
    ik3.value = "v02";
    ik4.value = "v10";
    ik5.value = "v07";
    ik6.value = "v20";
    mt.Put(ik1);
    mt.Put(ik2);
    mt.Put(ik3);
    mt.Put(ik4);
    mt.Put(ik5);
    mt.Put(ik6);
    // for (auto& [k, v] : kvs) {
    //     InternalKey ik(k);
    //     Record r(ik);
    //     r.value = v;
    //     mt.Put(r);
    //     std::cout << "Put(" << k << ", " << v << ")\n";
    // }

    // 3) 驗證 MemTable::Get()
    std::cout << "\n[Get() 測試]\n";
    auto check_get = [&](const std::string& k) {
        auto v = mt.Get(k);
        std::cout << "Get(" << k << ") -> ";
        if (v.has_value()) std::cout << *v << "\n";
        else std::cout << "(not found)\n";
    };
    check_get("k01");
    check_get("k02");
    check_get("k07");
    check_get("k09"); // 不存在
    check_get("k20");

    // 4) 重新建立 iterator（也可以沿用）
    MemTableIterator it2(list_sp, &icmp);

    // 5) 正向遍歷：SeekToFirst -> Next
    std::cout << "\n[正向遍歷 SeekToFirst/Next]\n";
    it2.SeekToFirst();
    while (it2.Valid()) {
        PrintKV(it2.key(), it2.value());
        it2.Next();
    }
    std::cout << "(到尾端) Valid() = " << (it2.Valid() ? "true" : "false") << "\n";

    // 6) 反向遍歷：SeekToLast -> Prev
    std::cout << "\n[反向遍歷 SeekToLast/Prev]\n";
    it2.SeekToLast();
    while (it2.Valid()) {
        PrintKV(it2.key(), it2.value());
        it2.Prev();
    }
    std::cout << "(到頭端) Valid() = " << (it2.Valid() ? "true" : "false") << "\n";

    // 7) Seek 定位測試
    std::cout << "\n[Seek 定位測試]\n";
    auto try_seek = [&](std::string target) {
        it2.Seek(target);
        std::cout << "Seek('" << target << "'): ";
        if (it2.Valid()) {
            std::cout << "命中/或下界 -> ";
            PrintKV(it2.key(), it2.value());
        } else {
            std::cout << "無效（可能 target 大於最大鍵）\n";
        }
    };
    try_seek("k00"); // 小於最小鍵
    try_seek("k01"); // 等於某鍵
    try_seek("k04"); // 介於 k03 與 k07 之間，看你的 Seek 是下界則應到 k07
    try_seek("k99"); // 大於最大鍵，應該 Invalid

    // 8) Min / Max Key（如果你的 getMinKey/getMaxKey 沒有處理空表，請自行加上判斷）
    std::cout << "\n[Min/Max Key 測試]\n";
    try {
        auto mink = mt.getMinKey(); // 若空表會崩，這裡目前非空
        auto maxk = mt.getMaxKey();
        std::cout << "MinKey = " << mink.UserKey() << "\n";
        std::cout << "MaxKey = " << maxk.UserKey() << "\n";
    } catch (...) {
        std::cout << "getMinKey/getMaxKey 可能在空表時崩潰，請改為回傳 std::optional\n";
    }

    // 9) ApproximateMemoryUsage / memTableIsFull / Dump（如有）
    std::cout << "\n[其他 API]\n";
    std::cout << "ApproximateMemoryUsage() = " << mt.ApproximateMemoryUsage() << " bytes (大略)\n";
    std::cout << "memTableIsFull() = " << (mt.memTableIsFull() ? "true" : "false") << "\n";
    std::cout << "Dump():\n";
    mt.Dump();

    std::cout << "\n=== 測試完成 ===\n";
    return 0;
}
