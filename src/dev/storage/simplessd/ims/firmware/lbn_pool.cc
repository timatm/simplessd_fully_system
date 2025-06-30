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
#include "def.hh"
#include "print.hh"
#include "persistence.hh"
#include "mapping_table.hh"
#include "log.hh"
#include "IMS_interface.hh"
// freeLBNList 操作
void LBNPool::reset_lbn_pool(){
    int used_LBN_num = 0;
    for (int ch = 0; ch < CHANNEL_NUM; ++ch) {
        while (!freeLBNList[ch].empty()) {
            freeLBNList[ch].pop_front();
        }
    }
    for (int ch = 0; ch < CHANNEL_NUM; ++ch) {
        while (!usedLBNList[ch].empty()) {
            usedLBNList[ch].pop_front();
        }
    }
}


int LBNPool::init_lbn_pool(int expect_used_LBN_num){
    int used_LBN_num = 0;
    for(auto [filename ,lbn] :mappingManager.mappingTable){
        int channel = LBN2CH(lbn);
        if (channel >= CHANNEL_NUM) {
            pr_info("Invalid channel %lu for LBN %lu in mapping table", channel, lbn);
            continue;
        }
        pr_info("MappingManager insert used LBN:%8lu to [CH]: %lu", lbn, channel);
        usedLBNList[channel].push_back(lbn);
        used_LBN_num++;
    }

    for(auto lbn:logManager.logRecordList){
        int channel = LBN2CH(lbn);
        if (channel >= CHANNEL_NUM) {
            pr_info("Invalid channel %lu for LBN %lu in mapping table", channel, lbn);
            continue;
        }
        pr_info("lbnManager insert used LBN:%8lu to [CH]: %lu", lbn, channel);
        usedLBNList[channel].push_back(lbn);
        used_LBN_num++;
    }
    for(uint64_t lbn = 0;lbn < LBN_NUM;lbn++){
        if (lbnPoolManager.get_freeLBNList(lbn)) {
            pr_info("LBN %lu is already in free list, skipping", lbn);
            continue;
        }
        if (lbnPoolManager.get_usedLBNList(lbn)) {
            pr_info("LBN %lu is already in used list, skipping", lbn);
            continue;
        }
        insert_freeLBNList(lbn); // 將 LBN 插入到 freeLBNList 中
    }
    if(used_LBN_num == expect_used_LBN_num){
        pr_info("LBN pool initialized successfully with %d used LBNs", expect_used_LBN_num);
        return OPERATION_SUCCESS;
    }
    pr_info("LBN pool initialized failed,used number:%d ,expect:%d", used_LBN_num,expect_used_LBN_num);
    return OPERATION_FAILURE;
}

void LBNPool::insert_freeLBNList(uint64_t lbn) {
    uint64_t ch = LBN2CH(lbn);
    uint64_t package = LBN2PACKAGE(lbn);
    uint64_t die = LBN2DIE(lbn);
    uint64_t plane = LBN2PLANE(lbn);
    // pr_info("insert free LBN:%8lu to [CH]: %lu [PACK]: %lu [DIE]: %lu [PLANE]: %lu", lbn, channel, package, die, plane);
    auto it = std::find(freeLBNList[ch].begin(),freeLBNList[ch].end(),lbn);
    if(it != freeLBNList[ch].end()){
        pr_debug("This LBN:%lld hae been in freeLBNList",lbn);
        return;
    }
    freeLBNList[ch].push_back(lbn);
    return;
}

bool LBNPool::remove_freeLBNList(uint64_t lbn) {
    uint64_t channel = LBN2CH(lbn);
    auto& deque = freeLBNList[channel];
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
    auto& deque = freeLBNList[channel];
    for (const auto& val : deque) {
        if (val == lbn)
            return true;
    }
    return false;
}

uint64_t LBNPool::getFront_freeLBNList(int ch){
    uint64_t lbn = freeLBNList[ch].front();
    return lbn;
}


uint64_t LBNPool::pop_freeLBNList(int ch){
    uint64_t lbn = freeLBNList[ch].front();
    freeLBNList[ch].pop_front();
    return lbn;
}

// usedLBNList 操作
void LBNPool::insert_usedLBNList(uint64_t lbn) {
    int ch = LBN2CH(lbn);
    auto it = std::find(usedLBNList[ch].begin(),usedLBNList[ch].end(),lbn);
    if(it != usedLBNList[ch].end()){
        pr_debug("This LBN:%lld hae been in usedLBNList",lbn);
        return;
    }
    usedLBNList[ch].push_back(lbn);
    pr_info("IMS insert LBN:%lld in CH[%d] to used list ",lbn,LBN2CH(lbn));
}

bool LBNPool::remove_usedLBNList(uint64_t lbn) {
    int ch = LBN2CH(lbn);
    auto &deque = usedLBNList[ch];
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
    auto& deque = usedLBNList[ch];
    for (const auto& val : deque) {
        if (val == lbn)
            return true;
    }
    return false;
}

void LBNPool::insert_valueLogList(uint64_t lbn){
    int ch = LBN2CH(lbn);
    auto it = std::find(valueLogList.begin(),valueLogList.end(),lbn);
    if(it != valueLogList.end()){
        pr_debug("This LBN:%lld hae been in valueLogList",lbn);
        return;
    }
    valueLogList.push_back(lbn);
}


void LBNPool::remove_valueLogList(uint64_t lbn){
    valueLogList.pop_front();
}


// TODO allocate policy need to modify 
uint64_t LBNPool::allocate_valueLog_block(){
    uint64_t lbn = INVALIDLBN;
    for (int ch = 0;ch < CHANNEL_NUM;ch++){
        if (freeLBNList[ch].size() != 0){
            lbn = pop_freeLBNList(ch);
            insert_usedLBNList(lbn);
            return lbn;
        }
    }
    return INVALIDLBN;
}
// 附加：debug print
void LBNPool::print() {
    pr_info("===== LBN Pool =====");
    pr_info("=== Used LBN List ===");
    for (auto lbn : usedLBNList)
        pr_info("%lu ", lbn);
    pr_info("\n");
    pr_info("=== Free LBN List ===");
    for (size_t ch = 0; ch < CHANNEL_NUM; ++ch) {
        pr_info("Channel %lu: ", ch);
        for (auto lbn : freeLBNList[ch])
            pr_info("%lu ", lbn);
        pr_info("\n");
    }
    pr_info("======================\n");
}

uint64_t LBNPool::worst_policy(){
    uint64_t lbn = INVALIDLBN;
    for (int ch = 0;ch < CHANNEL_NUM;ch++){
        if (freeLBNList[ch].size() != 0){
            lbn = pop_freeLBNList(ch);
            insert_usedLBNList(lbn);
            return lbn;
        }
    }
    return INVALIDLBN;
}

uint64_t LBNPool::RRpolicy(){
    uint64_t lbn = INVALIDLBN;

    int start_ch = (lastUsedChannel + 1) % CHANNEL_NUM;
    int ch = start_ch;

    do {
        if (!freeLBNList[ch].empty()) {
            lbn = pop_freeLBNList(ch);
            insert_usedLBNList(lbn);
            lastUsedChannel = ch; 
            return lbn;
        }
        ch = (ch + 1) % CHANNEL_NUM;
    } while (ch != start_ch);  // 繞一圈回到起點代表全部都檢查過
    pr_info("LBN pool(RRpolicy) doesn't have free LBN");
    return INVALIDLBN;

}

uint64_t LBNPool::level2CH(hostInfo info){
    int level = info.levelInfo;
    uint64_t lbn = INVALIDLBN;
    if (level < 0 || level >= CHANNEL_NUM) {
        pr_info("Invalid level index: %d", level);
        return INVALIDLBN;
    }
    if(!freeLBNList[level].empty()){
        lbn = pop_freeLBNList(level);
        insert_usedLBNList(lbn);
        return lbn;
    }
    else{
        pr_info("LBN pool(level2CH) doesn't have free LBN");
    }
    return INVALIDLBN;
}

uint64_t LBNPool::my_policy(hostInfo info) {
    uint64_t lbn = INVALIDLBN;
    std::shared_ptr<TreeNode> newNode = std::make_shared<TreeNode>(info.filename, info.levelInfo,info.rangeMin, info.rangeMax);
    tree.insert_node(newNode);
    std::shared_ptr<TreeNode> node = tree.find_node(info.filename, info.levelInfo, info.rangeMin, info.rangeMax);
    
    if (!node) {
        pr_info("Node not found for filename: %s", info.filename.c_str());
        return INVALIDLBN;
    }

    std::vector<int> relateList = tree.get_relate_ch_info(node);
    if (relateList.empty()) {
        pr_info("Related channel list is empty");
        return INVALIDLBN;
    }

    std::vector<int> indices(relateList.size());
    std::iota(indices.begin(),indices.end(),0);
    std::sort(indices.begin(),indices.end(),[&](int a ,int b){
        return relateList[a] < relateList[b];
    });

    for(int ch:indices){
        if (!freeLBNList[ch].empty()) { 
            lbn = getFront_freeLBNList(ch);
            pr_info("My policy selected LBN: %lu from channel: %d", lbn, ch);
            return lbn;
        }
    }

    return INVALIDLBN;
}

uint64_t LBNPool::select_lbn(int type,hostInfo info){
    uint64_t lbn = 0;
    switch(type){
        case WROSTCASE:
            lbn = worst_policy();
            break;
        case RR:
            lbn = RRpolicy();
            break;
        case LEVEL2CH:
            lbn = level2CH(info);
            break;
        case MYPOLICY:
            lbn = my_policy(info);
            break;
        default:
            pr_info("The type of policy is invalid ,check your pass parameter");
            return INVALIDLBN;
    }
    
    return lbn;
}

void LBNPool::clear(){
    for (auto& q : usedLBNList) q.clear();
    for (auto& q : freeLBNList) q.clear();
    return;
}