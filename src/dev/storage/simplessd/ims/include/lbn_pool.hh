#ifndef __LBN_POOL_HH__
#define __LBN_POOL_HH__


#include <string>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <cstdint>
#include <vector>
#include <deque>
#include <queue>
#include <memory>
#include "def.hh"

class LBNPool {
public:
    void reset_lbn_pool();
    int init_lbn_pool(const std::vector<uint64_t>& used_lbn_list);

    void insert_freeLBNList(uint64_t lbn);
    bool remove_freeLBNList(uint64_t lbn);
    bool get_freeLBNList(uint64_t lbn);
    uint64_t pop_freeLBNList(int ch);
    uint64_t getFront_freeLBNList(int ch);
    
    void insert_usedLBNList(uint64_t lbn);
    bool remove_usedLBNList(uint64_t lbn);
    bool get_usedLBNList(uint64_t lbn);

    void insert_valueLogList(uint64_t lbn);
    void remove_valueLogList(uint64_t lbn);
    uint64_t allocate_valueLog_block();

    void dump_LBNPool();
    uint64_t worst_policy();
    uint64_t RRpolicy();
    uint64_t RRpolicyForLog();
    uint64_t level2CH(int level);
    uint64_t my_policy(const RelateChInfo& relate_ch_list);
    void clear();
    uint8_t get_lastUsedChannel(){return lastUsedChannel_;};
    void set_lastUsedChannel(uint8_t ch) {lastUsedChannel_ = ch;}
    std::vector<uint32_t> get_channel_info(){return used_count_per_ch_;}
    const std::array<std::deque<uint64_t>, CHANNEL_NUM>& get_usedLBNList() const {
        return usedLBNList_;
    }
    std::deque<uint64_t>& get_freeLBNList_ref(int ch, int die){
        return freeLBNList_[ch][die];
    }

    uint64_t getFront_freeLBNList_chOnly(int ch) const;
    uint64_t pop_freeLBNList_chOnly(int ch);

    int pickDieRR(int ch, bool forLog);
    bool hasFreeInChannel(int ch) const;
private:
    std::array<std::deque<uint64_t>, CHANNEL_NUM> usedLBNList_;
    // std::array<std::deque<uint64_t>, CHANNEL_NUM> freeLBNList_;
    using LBNDeque = std::deque<uint64_t>;
    std::array<std::array<LBNDeque, DIE_NUM>, CHANNEL_NUM> freeLBNList_;
    std::vector<uint32_t> used_count_per_ch_; 
    std::deque<uint64_t> valueLogList_;
    uint8_t lastUsedChannel_;
    std::array<int, CHANNEL_NUM> lastUsedDie_;
    std::array<int, CHANNEL_NUM> lastUsedDie_log_;
    uint8_t lastUsedChannel_log;
};


#endif