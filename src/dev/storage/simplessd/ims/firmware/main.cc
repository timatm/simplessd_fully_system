#include "IMS_interface.hh"
#include <iostream>
#include <vector>
#include <cstring>
#include <random>
#include <ctime>
std::string random_key(size_t len) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static std::mt19937 rng(static_cast<unsigned>(time(nullptr)));
    static std::uniform_int_distribution<> dist_char(0, sizeof(charset) - 2);
    std::string result;
    for (size_t i = 0; i < len; ++i) {
        result += charset[dist_char(rng)];
    }
    return result;
}

// 產生長度介於 4～8 的隨機 key
std::string random_key_auto() {
    static std::mt19937 rng(static_cast<unsigned>(time(nullptr)) + 123);
    static std::uniform_int_distribution<> len_dist(4, 8);
    return random_key(len_dist(rng));
}

int main() {
    try {
        IMS_interface ims;

        std::cout << "[TEST] init_IMS()" << std::endl;
        // ims.reset_IMS();

        ims.init_IMS();

        // 模擬一筆資料寫入
        std::vector<uint8_t> buffer(IMS_PAGE_SIZE * IMS_PAGE_NUM, 0xAB);
        std::memcpy(buffer.data(), "HelloSSD", 8);

        std::vector<hostInfo> requests;
        for (int i = 0; i < 30; ++i) {
            std::string name = "sst_" + std::to_string(i + 1);

            std::string k1 = random_key_auto();
            std::string k2 = random_key_auto();
            if (k2 < k1) std::swap(k1, k2);

            int level = 0;
            int channel = i % 4;

            requests.emplace_back(name, level, channel, Key(k1), Key(k2));
        }
        // for(auto request :requests){
        //     std::cout << "[TEST] write_sstable()" << std::endl;
        //     ims.write_sstable(&request, buffer.data()); 
        // }
      
        // std::vector<uint8_t> read_buffer(buffer.size());
        // std::cout << "[TEST] read_sstable()" << std::endl;
        // ims.read_sstable(&requests[3], read_buffer.data());
        uint64_t lbn;
        ims.allocate_block(&lbn);
        ims.dump_log_mannger();
        ims.allocate_block(&lbn);
        ims.dump_log_mannger();
        ims.allocate_block(&lbn);
        ims.dump_log_mannger();
        ims.allocate_block(&lbn);
        ims.dump_log_mannger();
        ims.allocate_block(&lbn);
        ims.dump_log_mannger();
        ims.allocate_block(&lbn);
        ims.dump_log_mannger();
        ims.allocate_block(&lbn);
        ims.allocate_block(&lbn);
        ims.allocate_block(&lbn);
        ims.allocate_block(&lbn);
        ims.allocate_block(&lbn);
        ims.allocate_block(&lbn);
        ims.dump_log_mannger();
        // ims.close_IMS();
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception: " << e.what() << std::endl;
    }
    return 0;
}
