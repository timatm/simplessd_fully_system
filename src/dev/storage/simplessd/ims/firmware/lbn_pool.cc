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
#include <cmath>  
#include <sstream>

#include "tree.hh"
#include "print.hh"
#include "persistence.hh"
#include "mapping_table.hh"
#include "log.hh"
#include "IMS_interface.hh"


bool LBNPool::hasFreeInChannel(int ch) const {
    for (int die = 0; die < DIE_NUM; ++die) {
        if (!freeLBNList_[ch][die].empty()) return true;
    }
    return false;
}

int LBNPool::pickDieRR(int ch, bool forLog) {
    auto &last = forLog ? lastUsedDie_log_[ch] : lastUsedDie_[ch];

    int start = (last + 1) % DIE_NUM;
    int d = start;

    do {
        if (!freeLBNList_[ch][d].empty()) {
            last = d;           // 更新 RR 指標
            return d;
        }
        d = (d + 1) % DIE_NUM;
    } while (d != start);

    return -1; // 沒有任何 die 有 free
}

void LBNPool::reset_lbn_pool(){
    int used_LBN_num = 0;
    for (int ch = 0; ch < CHANNEL_NUM; ++ch) {
        for (int die = 0; die < DIE_NUM; ++die) {
            freeLBNList_[ch][die].clear();
        }
    }

    for (int ch = 0; ch < CHANNEL_NUM; ++ch) {
        while (!usedLBNList_[ch].empty()) {
            usedLBNList_[ch].pop_front();
        }
    }
}

uint64_t LBNPool::getFront_freeLBNList_chOnly(int ch) const {
    int best_die = -1;
    uint64_t best_lbn = UINT64_MAX;

    for (int die = 0; die < DIE_NUM; ++die) {
        if (freeLBNList_[ch][die].empty()) continue;
        uint64_t f = freeLBNList_[ch][die].front();
        if (best_die == -1 || f < best_lbn) {
            best_die = die;
            best_lbn = f;
        }
    }

    return (best_die == -1) ? INVALIDLBN : best_lbn;
}

uint64_t LBNPool::pop_freeLBNList_chOnly(int ch) {
    int best_die = -1;
    uint64_t best_lbn = UINT64_MAX;

    for (int die = 0; die < DIE_NUM; ++die) {
        if (freeLBNList_[ch][die].empty()) continue;
        uint64_t f = freeLBNList_[ch][die].front();
        if (best_die == -1 || f < best_lbn) {
            best_die = die;
            best_lbn = f;
        }
    }

    if (best_die == -1) return INVALIDLBN;
    freeLBNList_[ch][best_die].pop_front();
    return best_lbn;
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
    lastUsedChannel_ = CHANNEL_NUM - 1;
    lastUsedChannel_log = CHANNEL_NUM - 1;
    for (int ch = 0; ch < CHANNEL_NUM; ++ch) {
        lastUsedDie_[ch] = -1;
        lastUsedDie_log_[ch] = -1;
    }
    // 
    return used_LBN_num;
}

void LBNPool::insert_freeLBNList(uint64_t lbn) {
    int ch  = (int)LBN2CH(lbn);
    int die = (int)LBN2DIE(lbn);

    auto &dq = freeLBNList_[ch][die];

    auto it = std::find(dq.begin(), dq.end(), lbn);
    if (it != dq.end()) {
        pr_error("This LBN:%lld has been in freeLBNList_[%d][%d]", lbn, ch, die);
        return;
    }
    dq.push_back(lbn);
}


bool LBNPool::remove_freeLBNList(uint64_t lbn) {
    int ch  = (int)LBN2CH(lbn);
    int die = (int)LBN2DIE(lbn);

    auto &dq = freeLBNList_[ch][die];
    for (auto it = dq.begin(); it != dq.end(); ++it) {
        if (*it == lbn) {
            dq.erase(it);
            return true;
        }
    }
    return false;
}

bool LBNPool::get_freeLBNList(uint64_t lbn) {
    int ch  = (int)LBN2CH(lbn);
    int die = (int)LBN2DIE(lbn);

    auto &dq = freeLBNList_[ch][die];
    return std::find(dq.begin(), dq.end(), lbn) != dq.end();
}

uint64_t LBNPool::getFront_freeLBNList(int ch) {
    int die = pickDieRR(ch, /*forLog=*/false);
    if (die < 0) return INVALIDLBN;
    return freeLBNList_[ch][die].front();
}

uint64_t LBNPool::pop_freeLBNList(int ch) {
    int die = pickDieRR(ch, /*forLog=*/false);
    if (die < 0) return INVALIDLBN;

    uint64_t lbn = freeLBNList_[ch][die].front();
    freeLBNList_[ch][die].pop_front();
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
            if (used_count_per_ch_[ch] > 0) used_count_per_ch_[ch]--;
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
    for (int ch = 0; ch < CHANNEL_NUM; ++ch) {
        for (int die = 0; die < DIE_NUM; ++die) {
            pr_info("Free[CH:%d][DIE:%d] size=%zu", ch, die, freeLBNList_[ch][die].size());
        }
    }


    pr_info("======================\n");
}


uint64_t LBNPool::worst_policy(){
    uint64_t lbn = INVALIDLBN;
    for (int ch = 0;ch < CHANNEL_NUM;ch++){
        if (hasFreeInChannel(ch)){
            lbn = pop_freeLBNList(ch);
            insert_usedLBNList(lbn);
            return lbn;
        }
    }
    return INVALIDLBN;
}

uint64_t LBNPool::RRpolicyForLog(){
    uint64_t lbn = INVALIDLBN;

    int start_ch = (lastUsedChannel_log + 1) % CHANNEL_NUM;
    int ch = start_ch;

    do {
        if (hasFreeInChannel(ch)) {
            lbn = getFront_freeLBNList(ch);
            lastUsedChannel_log = ch; 
            return lbn;
        }
        ch = (ch + 1) % CHANNEL_NUM;
    } while (ch != start_ch);
    pr_error("LBN pool(RRpolicy) doesn't have free LBN");
    return INVALIDLBN;
}
uint64_t LBNPool::RRpolicy() {
    int start_ch = (lastUsedChannel_ + 1) % CHANNEL_NUM;
    int ch = start_ch;

    do {
        if (hasFreeInChannel(ch)) {
            uint64_t lbn = getFront_freeLBNList_chOnly(ch); 
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
    if (level < 0) {
        pr_error("Invalid level index: %d", level);
        return INVALIDLBN;
    }
    int ch = level % CHANNEL_NUM;
    if (hasFreeInChannel(ch)) {
        return getFront_freeLBNList(ch);
    }
    pr_error("LBN pool(level2CH) doesn't have free LBN");
    return INVALIDLBN;
}



uint64_t LBNPool::my_policy(const RelateChInfo& info) {
    uint64_t lbn = INVALIDLBN;

    if (info.inter.size() != CHANNEL_NUM ||
        info.intra.size() != CHANNEL_NUM) {
        pr_error("my_policy: RelateChInfo size mismatch, inter=%zu intra=%zu CHANNEL_NUM=%d",
                 info.inter.size(), info.intra.size(), CHANNEL_NUM);
        return INVALIDLBN;
    }

    std::vector<double> score(CHANNEL_NUM, 0.0);
    for (int c = 0; c < CHANNEL_NUM; ++c) {
        // score[c] = alpha_inter_ * static_cast<double>(info.inter[c])
        //          + alpha_intra_ * static_cast<double>(info.intra[c]);
        double inter_score = 0;
        for(int i = 0;i < info.inter[c].size();i++){
            if(info.inter[c][i] < info.node_level){
                inter_score += 1;
            }
            else if(info.inter[c][i] > info.node_level){
                int level_gap = info.inter[c][i] - info.node_level;
                inter_score += 1.0 / std::pow(LEVEL_TIMES, level_gap);
            }
            else{
                pr_error("Key range of same level can't overlap");
            }
        }
        score[c] =  inter_score + static_cast<double>(info.L0[c]);
    }

    int select_ch = -1;
    double best_score = 0.0;
    uint64_t best_usage = 0;

    for (int c = 0; c < CHANNEL_NUM; ++c) {
        if (!hasFreeInChannel(c)) continue;

        if (select_ch == -1) {
            select_ch  = c;
            best_score = score[c];
            best_usage = used_count_per_ch_[c];   // usage[c]
            continue;
        }
        if (score[c] < best_score) {
            select_ch  = c;
            best_score = score[c];
            best_usage = used_count_per_ch_[c];
        }
        else if (score[c] == best_score) {
            if (used_count_per_ch_[c] < best_usage) {
                select_ch  = c;
                best_score = score[c];
                best_usage = used_count_per_ch_[c];
            }
        }
    }

    if (select_ch == -1) {
        pr_error("LBNPool::my_policy: no free LBN in any channel");
        return INVALIDLBN;
    }

    lbn = getFront_freeLBNList(select_ch);
    pr_info("My policy selected LBN: %lu from channel: %d "
             "(score=%.3f, inter=%d, intra=%d, L0=%d, usage=%lu)",
             lbn, select_ch,
             best_score,
             info.inter[select_ch],
             info.intra[select_ch],
             info.L0[select_ch],
             best_usage);
    return lbn;
}



uint64_t LBNPool::my_policyL0(const RelateChInfo& info) {
    uint64_t lbn = INVALIDLBN;

    if (info.inter.size() != CHANNEL_NUM ||
        info.intra.size() != CHANNEL_NUM) {
        pr_error("my_policy: RelateChInfo size mismatch, inter=%zu intra=%zu CHANNEL_NUM=%d",
                 info.inter.size(), info.intra.size(), CHANNEL_NUM);
        return INVALIDLBN;
    }
    std::vector<double> score(CHANNEL_NUM, 0.0);
    std::vector<int> candidate;
    for (int c = 0; c < CHANNEL_NUM; ++c) {
        // score[c] = alpha_inter_ * static_cast<double>(info.inter[c])
        //          + alpha_intra_ * static_cast<double>(info.intra[c]);
        double inter_score = 0;
        for(int i = 0;i < info.inter[c].size();i++){
            if(info.inter[c][i] < info.node_level){
                inter_score += 1;
            }
            else if(info.inter[c][i] > info.node_level){
                int level_gap = info.inter[c][i] - info.node_level;
                inter_score += 1.0 / std::pow(LEVEL_TIMES, level_gap);
            }
            else{
                pr_error("Key range of same level can't overlap");
            }
        }
        if(info.L0[c] == 0) candidate.push_back(c);
        score[c] =  inter_score + static_cast<double>(info.L0[c]);
    }
    int select_ch = -1;
    double best_score = 0.0;
    uint64_t best_usage = 0;

    for (int i = 0; i < candidate.size(); ++i) {
        int c = candidate[i];
        if (!hasFreeInChannel(c)) continue;

        if (select_ch == -1) {
            select_ch  = c;
            best_score = score[c];
            best_usage = used_count_per_ch_[c];   // usage[c]
            continue;
        }
        if (score[c] < best_score ) {
            select_ch  = c;
            best_score = score[c];
            best_usage = used_count_per_ch_[c];
        }
        else if (score[c] == best_score) {
            if (used_count_per_ch_[c] < best_usage) {
                select_ch  = c;
                best_score = score[c];
                best_usage = used_count_per_ch_[c];
            }
        }
    }

    if (select_ch == -1) {
        pr_error("LBNPool::my_policy: no free LBN in any channel");
        return INVALIDLBN;
    }

    lbn = getFront_freeLBNList(select_ch);
    pr_info("My policy selected LBN: %lu from channel: %d "
             "(score=%.3f, inter=%d, intra=%d, L0=%d, usage=%lu)",
             lbn, select_ch,
             best_score,
             info.inter[select_ch],
             info.intra[select_ch],
             info.L0[select_ch],
             best_usage);
    return lbn;
}


void LBNPool::clear(){
    for (auto& q : usedLBNList_) q.clear();
    for (auto& deq : freeLBNList_){
        for(auto& q : deq) q.clear();
    } 
    return;
}