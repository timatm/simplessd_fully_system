#include <gtest/gtest.h>
#include "../src/lbn_pool.hh"
#include "../src/tree.hh"
TEST(LBN_pool, InsertAndGet) {
    LBNPool mapMgr;
    mapMgr.insert_usedLBNList(0);
    EXPECT_TRUE(mapMgr.get_usedLBNList(0));
}

TEST(LBN_pool, InsertAndGetMultiple) {
    LBNPool mapMgr;
    mapMgr.insert_usedLBNList(0);
    mapMgr.insert_usedLBNList(1);
    EXPECT_TRUE(mapMgr.get_usedLBNList(0));
    EXPECT_TRUE(mapMgr.get_usedLBNList(1));
}

TEST(LBN_pool, Remove) {
    LBNPool mapMgr;
    mapMgr.insert_usedLBNList(0);
    EXPECT_TRUE(mapMgr.get_usedLBNList(0));
    mapMgr.remove_usedLBNList(0);
    EXPECT_FALSE(mapMgr.get_usedLBNList(0));
}

TEST(LBN_pool, RemoveMultiple) {
    LBNPool mapMgr;
    mapMgr.insert_usedLBNList(0);
    mapMgr.insert_usedLBNList(1);
    EXPECT_TRUE(mapMgr.get_usedLBNList(0));
    EXPECT_TRUE(mapMgr.get_usedLBNList(1));
    mapMgr.remove_usedLBNList(0);
    EXPECT_FALSE(mapMgr.get_usedLBNList(0));
    EXPECT_TRUE(mapMgr.get_usedLBNList(1));
}

TEST(LBN_pool, RemoveNonExistent) {
    LBNPool mapMgr;
    mapMgr.insert_usedLBNList(0);
    EXPECT_TRUE(mapMgr.get_usedLBNList(0));
    mapMgr.remove_usedLBNList(1); // Attempt to remove a non-existent LBN
    EXPECT_TRUE(mapMgr.get_usedLBNList(0)); // Should still be present
}

TEST(LBN_pool, InsertFreeLBN) {
    LBNPool mapMgr;
    mapMgr.insert_freeLBNList(0);
    EXPECT_TRUE(mapMgr.get_freeLBNList(0));
}
TEST(LBN_pool, InsertFreeLBNMultiple) {
    LBNPool mapMgr;
    mapMgr.insert_freeLBNList(0);
    mapMgr.insert_freeLBNList(1);
    EXPECT_TRUE(mapMgr.get_freeLBNList(0));
    EXPECT_TRUE(mapMgr.get_freeLBNList(1));
}
TEST(LBN_pool, RemoveFreeLBN) {
    LBNPool mapMgr;
    mapMgr.insert_freeLBNList(0);
    EXPECT_TRUE(mapMgr.get_freeLBNList(0));
    mapMgr.remove_freeLBNList(0);
    EXPECT_FALSE(mapMgr.get_freeLBNList(0));
}

TEST(LBN_pool, RemoveFreeLBNMultiple) {
    LBNPool mapMgr;
    mapMgr.insert_freeLBNList(0);
    mapMgr.insert_freeLBNList(1);
    EXPECT_TRUE(mapMgr.get_freeLBNList(0));
    EXPECT_TRUE(mapMgr.get_freeLBNList(1));
    mapMgr.remove_freeLBNList(0);
    EXPECT_FALSE(mapMgr.get_freeLBNList(0));
    EXPECT_TRUE(mapMgr.get_freeLBNList(1));
}
TEST(LBN_pool, RemoveFreeLBNNonExistent) {
    LBNPool mapMgr;
    mapMgr.insert_freeLBNList(0);
    EXPECT_TRUE(mapMgr.get_freeLBNList(0));
    mapMgr.remove_freeLBNList(1); // Attempt to remove a non-existent LBN
    EXPECT_TRUE(mapMgr.get_freeLBNList(0)); // Should still be present
}