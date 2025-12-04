#include "lbn_pool.hh"
#include "def.hh"
#include <unordered_set>
#include <array>
#include <vector>
#include <queue>
#include <cstdint>
#include <iostream>
#include <algorithm>
#include <numeric>

#include "tree.hh"
#include "print.hh"
#include "persistence.hh"
#include "mapping_table.hh"
#include "log.hh"
#include "IMS_interface.hh"
// freeLBNList 操作
void LBNPool::reset_lbn_pool(){
    int used_LBN_num = 0;
    for (int ch = 0; ch < CHANNEL_NUM; ++ch) {
        while (!freeLBNList_[ch].empty()) {
            freeLBNList_[ch].pop_front();
        }
    }
    for (int ch = 0; ch < CHANNEL_NUM; ++ch) {
        while (!usedLBNList_[ch].empty()) {
            usedLBNList_[ch].pop_front();
        }
    }
}




int LBNPool::init_lbn_pool(const std::vector<uint64_t>& used_lbn_list) {
    int used_LBN_num = 0;
    used_count_per_ch_.clear();
    for(int i = 0;i < CHANNEL_NUM;i++){
        used_count_per_ch_.push_back(0);
    }
    for (uint64_t lbn : used_lbn_list) {
        int channel = LBN2CH(lbn);
        if (channel >= CHANNEL_NUM) {
            pr_error("Invalid channel %d for LBN %lu", channel, lbn);
            continue;
        }
        if(lbn != 0){
            usedLBNList_[channel].push_back(lbn);
            used_LBN_num++;
        }
    }

    for (uint64_t lbn = 0; lbn < LBN_NUM; lbn++) {
        if (get_freeLBNList(lbn) || get_usedLBNList(lbn)) {
            continue;
        }
        insert_freeLBNList(lbn);
    }
    // 
    return used_LBN_num;
}


void LBNPool::insert_freeLBNList(uint64_t lbn) {
    uint64_t ch = LBN2CH(lbn);
    uint64_t package = LBN2PACKAGE(lbn);
    uint64_t die = LBN2DIE(lbn);
    uint64_t plane = LBN2PLANE(lbn);
    // pr_info("insert free LBN:%8lu to [CH]: %lu [PACK]: %lu [DIE]: %lu [PLANE]: %lu", lbn, channel, package, die, plane);
    auto it = std::find(freeLBNList_[ch].begin(),freeLBNList_[ch].end(),lbn);
    if(it != freeLBNList_[ch].end()){
        pr_error("This LBN:%lld hae been in freeLBNList",lbn);
        return;
    }
    freeLBNList_[ch].push_back(lbn);
    return;
}

bool LBNPool::remove_freeLBNList(uint64_t lbn) {
    uint64_t channel = LBN2CH(lbn);
    auto& deque = freeLBNList_[channel];
    for (auto it = deque.begin(); it != deque.end(); ++it) {
        if (*it == lbn) {
            deque.erase(it);
            return true;
        }
    }
    return false; // not found
}

bool LBNPool::get_freeLBNList(uint64_t lbn) {
    uint64_t channel = LBN2CH(lbn);
    auto& deque = freeLBNList_[channel];
    for (const auto& val : deque) {
        if (val == lbn)
            return true;
    }
    return false;
}

uint64_t LBNPool::getFront_freeLBNList(int ch){
    uint64_t lbn = freeLBNList_[ch].front();
    return lbn;
}


uint64_t LBNPool::pop_freeLBNList(int ch){
    uint64_t lbn = freeLBNList_[ch].front();
    freeLBNList_[ch].pop_front();
    return lbn;
}

void LBNPool::insert_usedLBNList(uint64_t lbn) {
    if(lbn < 0 || lbn > LBN_NUM){
        pr_error("insert_usedLBNList: invalid LBN=%llu", lbn);
        return;
    }
    int ch = LBN2CH(lbn);
    if (ch < 0 || ch >= CHANNEL_NUM) {
        pr_error("insert_usedLBNList: invalid ch=%d for LBN=%llu", ch, lbn);
        return;
    }
    auto it = std::find(usedLBNList_[ch].begin(),usedLBNList_[ch].end(),lbn);
    if(it != usedLBNList_[ch].end()){
        pr_error("This LBN:%lld have been in usedLBNList",lbn);
        return;
    }
    usedLBNList_[ch].push_back(lbn);
    used_count_per_ch_[ch]++;
    pr_debug("IMS insert LBN:%lld in CH[%d] to used list ",lbn,LBN2CH(lbn));
}

bool LBNPool::remove_usedLBNList(uint64_t lbn) {
    int ch = LBN2CH(lbn);
    auto &deque = usedLBNList_[ch];
    for (auto it = deque.begin(); it != deque.end(); ++it) {
        if (*it == lbn) {
            deque.erase(it);
            return true;
        }
    }
    return false;
}

bool LBNPool::get_usedLBNList(uint64_t lbn) {
    int ch = LBN2CH(lbn);
    auto& deque = usedLBNList_[ch];
    for (const auto& val : deque) {
        if (val == lbn)
            return true;
    }
    return false;
}

void LBNPool::insert_valueLogList(uint64_t lbn){
    int ch = LBN2CH(lbn);
    auto it = std::find(valueLogList_.begin(),valueLogList_.end(),lbn);
    if(it != valueLogList_.end()){
        pr_error("This LBN:%lld hae been in valueLogList",lbn);
        return;
    }
    valueLogList_.push_back(lbn);
}


void LBNPool::remove_valueLogList(uint64_t lbn){
    valueLogList_.pop_front();
}


// TODO allocate policy need to modify 
uint64_t LBNPool::allocate_valueLog_block(){
    uint64_t lbn = INVALIDLBN;
    for (int ch = 0;ch < CHANNEL_NUM;ch++){
        if (freeLBNList_[ch].size() != 0){
            lbn = pop_freeLBNList(ch);
            insert_usedLBNList(lbn);
            return lbn;
        }
    }
    return INVALIDLBN;
}
// 附加：debug print
void LBNPool::dump_LBNPool() {
    pr_info("");
    pr_info("[LBN Pool]");

    pr_info("=== Used LBN List ===");
    for (size_t ch = 0; ch < CHANNEL_NUM; ++ch) {
        std::ostringstream oss;
        pr_info("Channel[%d]:",ch);

        int cnt = 0;
        for (auto lbn : usedLBNList_[ch]) {
            oss << lbn << " ";
            if (++cnt % 16 == 0) {
                pr_info("%s", oss.str().c_str());
                oss.str("");  // 清空
                oss.clear();
            }
        }
        if (cnt % 16 != 0) {
            pr_info("%s", oss.str().c_str());
        }
    }

    pr_info("=== Free LBN List ===");
    for (size_t ch = 0; ch < CHANNEL_NUM; ++ch) {
        std::ostringstream oss;
        pr_info("Channel[%d]:",ch);

        int cnt = 0;
        for (auto lbn : freeLBNList_[ch]) {
            oss << lbn << " ";
            if (++cnt % 16 == 0) {
                pr_info("%s", oss.str().c_str());
                oss.str("");
                oss.clear();
            }
        }
        if (cnt % 16 != 0) {
            pr_info("%s", oss.str().c_str());
        }
    }

    pr_info("======================\n");
}


uint64_t LBNPool::worst_policy(){
    uint64_t lbn = INVALIDLBN;
    for (int ch = 0;ch < CHANNEL_NUM;ch++){
        if (freeLBNList_[ch].size() != 0){
            lbn = pop_freeLBNList(ch);
            insert_usedLBNList(lbn);
            return lbn;
        }
    }
    return INVALIDLBN;
}

uint64_t LBNPool::RRpolicy(){
    uint64_t lbn = INVALIDLBN;

    int start_ch = (lastUsedChannel_ + 1) % CHANNEL_NUM;
    int ch = start_ch;

    do {
        if (!freeLBNList_[ch].empty()) {
            lbn = getFront_freeLBNList(ch);
            lastUsedChannel_ = ch; 
            return lbn;
        }
        ch = (ch + 1) % CHANNEL_NUM;
    } while (ch != start_ch);
    pr_error("LBN pool(RRpolicy) doesn't have free LBN");
    return INVALIDLBN;

}

uint64_t LBNPool::level2CH(int level){
    uint64_t lbn = INVALIDLBN;
    if (level < 0 || level >= CHANNEL_NUM) {
        pr_error("Invalid level index: %d", level);
        return INVALIDLBN;
    }
    if(!freeLBNList_[level].empty()){
        lbn = getFront_freeLBNList(level);
        return lbn;
    }
    else{
        pr_error("LBN pool(level2CH) doesn't have free LBN");
    }
    return INVALIDLBN;
}
// uint64_t LBNPool::my_policy(const std::vector<int>& relate_ch_list) {
//     uint64_t lbn = INVALIDLBN;

//     std::vector<int> indices(relate_ch_list.size());
//     std::iota(indices.begin(), indices.end(), 0);

//     std::sort(indices.begin(), indices.end(), [&](int a, int b) {
//         if(relate_ch_list[a] == relate_ch_list[b]){
//             return used_count_per_ch_[a] < used_count_per_ch_[b];
//         }
//         return relate_ch_list[a] < relate_ch_list[b];
//     });

//     for (int i : indices) {
//         int ch = relate_ch_list[i];
//         if (!freeLBNList_[ch].empty()) {
//             lbn = getFront_freeLBNList(ch);
//             pr_debug("My policy selected LBN: %lu from channel: %d", lbn, ch);
//             return lbn;
//         }
//     }

//     return INVALIDLBN;
// }
uint64_t LBNPool::my_policy(const std::vector<int>& relate_ch_info) {
    uint64_t lbn = INVALIDLBN;

    // channels = [0, 1, 2, ..., num_channels-1]
    std::vector<int> channels(relate_ch_info.size());
    std::iota(channels.begin(), channels.end(), 0);

    // 排序規則：
    // 1) relate_ch_info 小的 channel 排前面
    // 2) 若 relate_ch_info 一樣，used_count_per_ch_ 小的排前面
    std::sort(channels.begin(), channels.end(),
              [&](int a, int b) {
                  if (relate_ch_info[a] == relate_ch_info[b]) {
                      return used_count_per_ch_[a] < used_count_per_ch_[b];
                  }
                  return relate_ch_info[a] < relate_ch_info[b];
              });

    // 照排序後的 channel 順序找第一個有 free LBN 的
    for (int ch : channels) {
        if (!freeLBNList_[ch].empty()) {
            lbn = getFront_freeLBNList(ch);
            pr_debug("My policy selected LBN: %lu from channel: %d", lbn, ch);
            return lbn;
        }
    }

    return INVALIDLBN;
}




void LBNPool::clear(){
    for (auto& q : usedLBNList_) q.clear();
    for (auto& q : freeLBNList_) q.clear();
    return;
}