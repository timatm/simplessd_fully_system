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
    std::array<std::deque<uint64_t>, CHANNEL_NUM> usedLBNList;
    std::array<std::deque<uint64_t>, CHANNEL_NUM> freeLBNList;
    std::deque<uint64_t> valueLogList;
    int lastUsedChannel;

    void reset_lbn_pool();
    int init_lbn_pool(int);
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

    uint64_t select_lbn(int type,hostInfo info);
    void print();
    uint64_t worst_policy();
    uint64_t RRpolicy();
    uint64_t level2CH(hostInfo info);
    uint64_t my_policy(hostInfo info);
    void clear();
    enum{
        WROSTCASE = 0,
        RR        = 1,
        LEVEL2CH  = 2,
        MYPOLICY  = 3
    };
};

extern LBNPool lbnPoolManager; 

#endif