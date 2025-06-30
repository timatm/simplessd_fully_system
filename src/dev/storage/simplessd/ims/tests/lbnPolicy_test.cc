#include <gtest/gtest.h>
#include "../src/lbn_pool.hh"
#include "../src/def.hh"
#include "../src/IMS_interface.hh"
#include "../src/tree.hh"
#include <iostream>
#include <iomanip>

#include <queue>
#include <unordered_map>
#include <vector>
#include <random>

std::string input = R"(
insert A0 1 1 1 9
insert A1 1 3 10 18
insert A2 1 1 19 25
insert A3 1 3 26 36
insert A4 1 0 37 47
insert A5 1 2 48 54
insert A6 1 0 55 61
insert A7 1 1 62 70
insert A8 1 3 71 78
insert A9 1 3 79 89
insert A10 1 3 90 96
insert A11 2 3 1 10
insert A12 2 3 11 16
insert A13 2 2 17 27
insert A14 2 1 28 38
insert A15 2 0 39 47
insert A16 2 3 48 53
insert A17 2 2 54 59
insert A18 2 1 60 67
insert A19 2 0 68 78
insert A20 2 0 79 87
insert A21 3 2 1 6
insert A22 3 3 7 12
insert A23 3 1 13 22
insert A24 3 3 23 29
insert A25 3 0 30 39
insert A26 3 1 40 46
insert A27 3 2 47 53
insert A28 4 2 1 8
insert A29 4 1 9 17
insert A30 4 1 18 28
insert A31 4 0 29 37
insert A32 4 0 38 46
insert A33 5 3 1 6
insert A34 5 2 7 15
insert A35 5 0 16 26
insert A36 5 2 27 36
insert A37 5 3 37 45
insert A38 6 1 1 7
insert A39 6 3 8 16
insert A40 6 0 17 27
insert A41 6 2 28 36
insert A42 6 1 37 45
insert A43 7 3 1 10
insert A45 7 0 22 32
insert A46 7 2 33 41
insert A47 7 3 42 50
insert A48 7 1 51 58
insert A49 7 1 59 66
)";

TEST(LBN_policy,wrost){
    LBNPool pool;
    pool.reset_lbn_pool();
    for(uint64_t lbn;lbn < LBN_NUM;lbn++){
        pool.insert_freeLBNList(lbn);
    }

    for(uint64_t i;i < LBN_NUM;i++){
        EXPECT_EQ(pool.worst_policy(), i);
        EXPECT_TRUE(pool.get_usedLBNList(i));
        EXPECT_FALSE(pool.get_freeLBNList(i));
    }
};  




TEST(LBN_policy, RR_policy) {
    LBNPool pool;
    pool.reset_lbn_pool();
    for(uint64_t lbn = 0;lbn < LBN_NUM;lbn++){
        pool.insert_freeLBNList(lbn);
    }
    // caculate the expected LBN
    int nextCH = (pool.lastUsedChannel+1) % CHANNEL_NUM;
    while(!pool.freeLBNList[nextCH].empty()){
        uint64_t lbn = pool.RRpolicy();
        int ch = LBN2CH(lbn);
        EXPECT_EQ(ch,nextCH);
        EXPECT_TRUE(pool.get_usedLBNList(lbn));
        EXPECT_FALSE(pool.get_freeLBNList(lbn));
        nextCH = (pool.lastUsedChannel+1) % CHANNEL_NUM;
    }
}

TEST(LBN_policy, levelpolicy) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, CHANNEL_NUM-1);
    LBNPool pool;
    pool.reset_lbn_pool();
    for(uint64_t lbn = 0;lbn < LBN_NUM;lbn++){
        pool.insert_freeLBNList(lbn);
    }
    hostInfo info("dummy" ,0,0,0);
    uint64_t lbn = 0;
    while(lbn != INVALIDLBN){
        lbn = pool.level2CH(info);
        if(lbn != INVALIDLBN){
            int ch = LBN2CH(lbn);
            EXPECT_EQ(ch,info.levelInfo);
            EXPECT_TRUE(pool.get_usedLBNList(lbn));
            EXPECT_FALSE(pool.get_freeLBNList(lbn));
            info.levelInfo = dist(gen);
        }   
    }
}
void execute_tree_test(Tree& tree, std::string& commands) {
    std::istringstream iss(commands);
    std::string line;
    while (std::getline(iss, line)) {
        std::istringstream linestream(line);
        std::string cmd;
        linestream >> cmd;

        if (cmd == "insert") {
            std::string filename;
            int level, min, max,ch;
            linestream >> filename >> level >> ch >> min >> max;
            std::cout << "[DEBUG] Insert " << filename << std::endl;
            auto node = std::make_shared<TreeNode>(filename, level,ch, min, max);
            tree.insert_node(node);
        }   
        else {
            std::cout << "[DEBUG] Unknown command: " << cmd << "\n";

        }
    }
}


TEST(LBN_policy, my_policy) {
    Tree tree;
    LBNPool pool;
    pool.reset_lbn_pool();
    for(uint64_t lbn = 0;lbn < LBN_NUM;lbn++){
        pool.insert_freeLBNList(lbn);
    }
    // caculate the expected LBN
    execute_tree_test(tree,input);
    auto node = std::make_shared<TreeNode>("B",7,11,20);
    tree.insert_node(node);
    auto vec = tree.get_relate_ch_info(node);

    auto nodes = tree.level_map[7];
    for(auto n:nodes){
        std::cout << n->filename <<  "  "<< "\n";
    }

    EXPECT_EQ(vec[0],3);
    EXPECT_EQ(vec[1],4);
    EXPECT_EQ(vec[2],2);
    EXPECT_EQ(vec[3],5);
}