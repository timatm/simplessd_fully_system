#include "read_cache.hh"
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

using cache::ReadCache;

// 一個示範：Value=std::string（用預設 sizer：.size()）
// 使用者可自訂容量（bytes）
int main(int argc, char** argv) {
    size_t capacity = 64 * 1024; // 預設 64KB
    if (argc >= 2) {
        capacity = std::stoull(argv[1]);
    }
    std::cout << "[Demo] ReadCache capacity = " << capacity << " bytes\n";

    ReadCache<std::string, std::string> rc(capacity);

    auto loader = [](const std::string& key) -> std::string {
        // 模擬從磁碟/裝置讀取：延遲 + 生成資料
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return "VALUE_FOR_" + key; // 真實場景換成實際讀取
    };

    // 讀一些 key
    for (int round = 0; round < 2; ++round) {
        for (int i = 0; i < 10; ++i) {
            std::string key = "k" + std::to_string(i);
            auto before = std::chrono::steady_clock::now();
            std::string val = rc.GetOrLoad(key, loader);
            auto after = std::chrono::steady_clock::now();

            auto us = std::chrono::duration_cast<std::chrono::microseconds>(after - before).count();
            std::cout << "GetOrLoad(" << key << ") -> " << val
                      << " | elapsed(us)=" << us << "\n";
        }
        std::cout << "Cache size(bytes)=" << rc.SizeBytes()
                  << " / capacity=" << rc.CapacityBytes() << "\n";
    }

    // 手動 Put/Erase 範例
    rc.Put("big", std::string(32768, 'B')); // 32KB
    std::cout << "After Put(big,32KB) size=" << rc.SizeBytes() << "\n";

    rc.Erase("k0");
    std::cout << "After Erase(k0) size=" << rc.SizeBytes() << "\n";

    // 調整容量（若縮小會觸發逐出）
    rc.SetCapacity(16 * 1024);
    std::cout << "After SetCapacity(16KB) size=" << rc.SizeBytes()
              << " / cap=" << rc.CapacityBytes() << "\n";

    return 0;
}
