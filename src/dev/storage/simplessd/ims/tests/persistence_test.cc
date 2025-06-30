#include <gtest/gtest.h>
#include <cstring>
#include <unordered_map>
#include <string>
#include <list>

#include "../src/persistence.hh"
#include "../src/mapping_table.hh"
#include "../src/disk.hh"
#include "../src/print.hh"
#include "../src/IMS_interface.hh"
constexpr const char* test_file = "test_disk.bin";

class PersistenceTest : public ::testing::Test {
protected:
    Persistence persistence;
    std::string filename = test_file;

    void SetUp() override {
        persistence.disk.open(filename);
    }

    void TearDown() override {
        persistence.disk.close();
        std::remove(filename.c_str());
    }

    void fill_buffer(uint8_t* buffer, uint8_t value, size_t size) {
        for (size_t i = 0; i < size; ++i) {
            buffer[i] = value;
        }
    }
};

// 測試 writeSStable / readSStable
TEST_F(PersistenceTest, ReadWriteSStableBlock) {
    uint8_t write_buf[BLOCK_SIZE];
    uint8_t read_buf[BLOCK_SIZE];
    fill_buffer(write_buf, 0xA5, BLOCK_SIZE);

    EXPECT_EQ(persistence.flushSStable(2, write_buf, BLOCK_SIZE), OPERATION_SUCCESS);
    
    EXPECT_EQ(persistence.readSStable(2, read_buf, BLOCK_SIZE), OPERATION_SUCCESS);
    
    EXPECT_EQ(memcmp(write_buf, read_buf, BLOCK_SIZE), 0);
}

// 測試單頁讀取 readSStablePage
TEST_F(PersistenceTest, ReadWriteSStablePage) {
    uint8_t write_buf[BLOCK_SIZE];
    uint8_t read_buf[PAGE_SIZE];
    fill_buffer(write_buf, 0x3C, BLOCK_SIZE);
    
    uint8_t expect[PAGE_SIZE];
    fill_buffer(expect, 0x3C, PAGE_SIZE);
    uint64_t lbn = 3;

    uint64_t offset = 10;
    uint64_t lpn = LBN2LPN(lbn) + offset;
    
    ASSERT_EQ(persistence.flushSStable(lbn, write_buf, BLOCK_SIZE), 0);
    ASSERT_EQ(persistence.readSStablePage(lpn, read_buf, PAGE_SIZE), OPERATION_SUCCESS);
    ASSERT_EQ(memcmp(expect, read_buf, PAGE_SIZE), 0);
    ASSERT_EQ(persistence.readSStablePage(lpn-1, read_buf, PAGE_SIZE), OPERATION_SUCCESS);
    ASSERT_EQ(memcmp(expect, read_buf, PAGE_SIZE), 0);
    ASSERT_EQ(persistence.readSStablePage(lpn+1, read_buf, PAGE_SIZE), OPERATION_SUCCESS);
    ASSERT_EQ(memcmp(expect, read_buf, PAGE_SIZE), 0);
}

// 測試 mappingTable 的 flush / read 功能
TEST_F(PersistenceTest, ReadWriteMappingTable) {
    sp_ptr_old->mapping_store = 0;          //   ★ 保證寫到 LPN 0

    std::unordered_map<std::string, uint64_t> mappingTable = {
        {"file1.sst", 10},
        {"file2.sst", 11},
        {"file3.sst", 12}
    };
    ASSERT_EQ(persistence.flushMappingTable(mappingTable), OPERATION_SUCCESS);

    uint8_t buffer[PAGE_SIZE];
    ASSERT_EQ(persistence.readMappingTable(0, buffer, PAGE_SIZE), OPERATION_SUCCESS);

    auto* mp = reinterpret_cast<mappingTablePerPage*>(buffer);
    EXPECT_EQ(mp->entry_num, 3) << "entry_num 應該是 3";

    bool f1=false,f2=false,f3=false;
    for (int i=0;i<mp->entry_num;++i) {
        std::string name = mp->entry[i].fileName;
        uint64_t    lbn  = mp->entry[i].lbn;
        if (name=="file1.sst" && lbn==10) f1 = true;
        if (name=="file2.sst" && lbn==11) f2 = true;
        if (name=="file3.sst" && lbn==12) f3 = true;
    }
    EXPECT_TRUE(f1);
    EXPECT_TRUE(f2);
    EXPECT_TRUE(f3);
}
