#include <gtest/gtest.h>
#include "../src/disk.hh"
#include <cstring>
#include <fstream>

class DiskTest : public ::testing::Test {
protected:
    Disk disk;
    std::string test_file = "test_disk.bin";

    void SetUp() override {
        disk.open(test_file);
    }

    void TearDown() override {
        disk.close();
        std::remove(test_file.c_str()); // 清除測試檔案
    }

    void fill_buffer(uint8_t* buffer, uint8_t value) {
        for (int i = 0; i < PAGE_SIZE; i++) {
            buffer[i] = value;
        }
    }
};

TEST_F(DiskTest, OpenAndCloseDisk) {
    // 不會丟出例外表示成功
    SUCCEED();
}

TEST_F(DiskTest, WriteAndReadPage) {
    uint8_t write_buf[PAGE_SIZE];
    uint8_t read_buf[PAGE_SIZE];
    fill_buffer(write_buf, 0xAB);

    ASSERT_EQ(disk.write(0, write_buf), 0);
    ASSERT_EQ(disk.read(0, read_buf), 0);
    ASSERT_EQ(memcmp(write_buf, read_buf, PAGE_SIZE), 0);
}

TEST_F(DiskTest, WriteAndReadBlock) {
    uint8_t block_buf[PAGE_SIZE * IMS_PAGE_NUM];
    uint8_t read_buf[PAGE_SIZE * IMS_PAGE_NUM];
    memset(block_buf, 0xCD, PAGE_SIZE * IMS_PAGE_NUM);

    uint64_t lbn = 3;
    ASSERT_EQ(disk.writeBlock(lbn, block_buf), 0);
    ASSERT_EQ(disk.readBlock(lbn, read_buf), 0);
    ASSERT_EQ(memcmp(block_buf, read_buf, PAGE_SIZE * IMS_PAGE_NUM), 0);
}


