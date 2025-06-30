#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <memory>

#include "../src/def.hh"
#include "../src/IMS_interface.hh"
#include "../src/mapping_table.hh"
#include "../src/persistence.hh"
#include "../src/lbn_pool.hh"
#include "../src/tree.hh"


constexpr const char* test_file = "test.bin";
// 測試專用 Fixture 類別
class IMSInterfaceTest : public ::testing::Test {
protected:
    IMS_interface ims;
    std::string filename = test_file;
    void SetUp() override {
        for(uint64_t lbn = 0;lbn < BLOCK_NUM;lbn++){
            lbnPoolManager.insert_freeLBNList(lbn);
        }
        
        persistenceManager.disk.open(filename);
    }

    void TearDown() override {
        persistenceManager.disk.close();
        std::remove(filename.c_str());
    }
    hostInfo make_request(const std::string& filename, int level, int rmin, int rmax) {
        hostInfo h(filename, level, -1, rmin, rmax);
        return h;
    }
};

TEST_F(IMSInterfaceTest, WriteThenReadSuccess) {
    uint8_t write_buf[BLOCK_SIZE];
    uint8_t read_buf[BLOCK_SIZE];
    std::memset(write_buf, 0x42, BLOCK_SIZE);  
    std::memset(read_buf, 0x00, BLOCK_SIZE);
    std::string test_filename = "001.sst";
    hostInfo req = make_request(test_filename, 1, 0, 10);
    EXPECT_EQ(ims.write_sstable(req, write_buf), OPERATION_SUCCESS);
    EXPECT_EQ(ims.read_sstable(req, read_buf), OPERATION_SUCCESS);
    EXPECT_EQ(std::memcmp(write_buf, read_buf, BLOCK_SIZE), 0);
    EXPECT_EQ(req.lbn, mappingManager.getLBN(test_filename));
    EXPECT_TRUE(lbnPoolManager.get_usedLBNList(req.lbn));
    EXPECT_FALSE(lbnPoolManager.get_freeLBNList(req.lbn));
    auto node = tree.find_node(test_filename);
    EXPECT_TRUE(node != nullptr);
}

TEST_F(IMSInterfaceTest, WriteNullBufferFails) {
    std::string test_filename = "001.sst";
    hostInfo req = make_request(test_filename, 1, 0, 10);
    ASSERT_EQ(ims.write_sstable(req, nullptr), OPERATION_FAILURE);
}


TEST_F(IMSInterfaceTest, ReadNullBufferFails) {
    std::string test_filename = "001.sst";
    hostInfo req = make_request(test_filename, 1, 0, 10);
    mappingManager.mappingTable[test_filename] = 1234; // 模擬已存在 entry
    ASSERT_EQ(ims.read_sstable(req, nullptr), OPERATION_FAILURE);
}

TEST_F(IMSInterfaceTest, ReadNonExistentFileFails) {
    hostInfo req = make_request("nonexistent.sst", 0, 0, 0);
    uint8_t buffer[BLOCK_SIZE];
    ASSERT_EQ(ims.read_sstable(req, buffer), OPERATION_FAILURE);
}

TEST_F(IMSInterfaceTest, DuplicateWriteFails) {
    uint8_t buffer[BLOCK_SIZE];
    std::memset(buffer, 0x66, BLOCK_SIZE);
    std::string test_filename = "001.sst";
    hostInfo req = make_request(test_filename, 1, 0, 10);

    ASSERT_EQ(ims.write_sstable(req, buffer), OPERATION_SUCCESS);
    ASSERT_EQ(ims.write_sstable(req, buffer), OPERATION_FAILURE); // 第二次應該失敗
}
