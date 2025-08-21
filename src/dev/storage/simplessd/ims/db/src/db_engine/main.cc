#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include "nvme_interface.hh"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "main.hh"
#include <iomanip>   
#include <cstdint>    
#include "db_api.hh"
#include "level_iter.hh"
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


#define CHECK(x) do { if (!(x)) { \
  std::cerr << "[CHECK FAILED] " #x " @ " << __LINE__ << "\n"; std::exit(2);} } while(0)

// ------------ 基于你比较器假设的 InternalKey 构造与打印 ------------
// 这里假设 InternalKey 拥有：key.key_size、key.key[]、info.seq、info.type 字段，且可平铺内存
static std::string MakeIKey(const InternalKeyComparator& icmp,
                            const std::string& user_key,
                            uint64_t seq,
                            ValueType type) {
    (void)icmp; // 仅用于签名一致性
    static_assert(std::is_trivially_copyable_v<InternalKey>,
                  "InternalKey must be trivially copyable");
    InternalKey ik(user_key,seq,type);
    std::string s(sizeof(InternalKey), '\0');
    std::memcpy(s.data(), &ik, sizeof(InternalKey));
    return s;
}

static void PrettyPrintIKey(const InternalKeyComparator&,
                            std::string_view sv) {
    CHECK(sv.size() == sizeof(InternalKey));
    InternalKey ik{};
    std::memcpy(&ik, sv.data(), sv.size());
    std::string uk(ik.key.key, ik.key.key + ik.key.key_size);
    std::cout << "{uk='" << uk << "', seq=" << ik.info.seq
              << ", type=" << (int)ik.info.type << "}";
}

static void ScanForward(Level0Iterator& it, const InternalKeyComparator& icmp) {
    std::cout << "\n[Forward]\n";
    for (; it.Valid(); it.Next()) {
        auto k = it.key();
        PrettyPrintIKey(icmp, k);
        std::string v;
        auto s = it.ReadValue(v);
        std::cout << " -> " << (s.ok() ? v : s.ToString()) << "\n";
    }
}

static void ScanBackward(Level0Iterator& it, const InternalKeyComparator& icmp) {
    std::cout << "\n[Backward]\n";
    for (; it.Valid(); it.Prev()) {
        auto k = it.key();
        PrettyPrintIKey(icmp, k);
        std::string v;
        auto s = it.ReadValue(v);
        std::cout << " -> " << (s.ok() ? v : s.ToString()) << "\n";
    }
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
    Options opts;
    opts.lower = InternalKey("key0").Encode(); // 假设你有一个 lower bound
    opts.upper = InternalKey("key20").Encode();
    Level0Iterator it(db.getSSTable(), db.getLogManager(),&icmp, db.getLSMTree(), opts);

    it.Init();
    // 4) 覆盖所有公开方法

    // 4.1 SeekToFirst + Forward
    it.SeekToFirst();
    CHECK(it.status().ok());
    ScanForward(it, icmp);

    // 4.2 SeekToLast + Backward
    it.SeekToLast();
    CHECK(it.status().ok());
    ScanBackward(it, icmp);

    // 4.3 Seek 到 >= 'b' 的位置，再 Forward
    {
        auto tgt = MakeIKey(icmp, "b", 0ull, ValueType::kTypeMin);
        std::cout << "\n[Seek to >= 'b', then forward]\n";
        it.Seek(tgt);
        ScanForward(it, icmp);
    }

    // 4.4 Prev()：从当前起向前走 3 步，验证重建逻辑
    {
        auto cur = MakeIKey(icmp, "c", 0ull, ValueType::kTypeMin);
        it.Seek(cur);
        if (!it.Valid()) it.SeekToLast();
        std::cout << "\n[From current, Prev() 3 steps]\n";
        for (int t = 0; t < 3 && it.Valid(); ++t) {
            PrettyPrintIKey(icmp, it.key()); std::cout << "\n";
            it.Prev();
        }
    }


    // 4.6 ReadValue() 单独验证
    {
        it.SeekToFirst();
        if (it.Valid()) {
            std::string v;
            auto s = it.ReadValue(v);
            CHECK(s.ok());
            std::cout << "\n[ReadValue] ";
            PrettyPrintIKey(icmp, it.key());
            std::cout << " -> " << v << "\n";
        }
    }

    
    std::cout << "\n[All tests done]\n";
    return 0;

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



