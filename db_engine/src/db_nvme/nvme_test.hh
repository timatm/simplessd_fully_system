#ifndef __NVME_TEST_HH__
#define __NVME_TEST_HH__

#include "nvme_interface.hh"
#include "IMS_interface.hh"
class MyNVMeDriver : public INVMEDriver {
public:

    int nvme_write_sstable(sstable_info ,char *buffer) override;
    int nvme_read_sstable(std::string ,char *buffer) override;
    int nvme_erase_sstable(std::string) override ;


    int nvme_compaction_io(const CompactionIOSimMeta& meta) override;
    // int nvme_ims_init() override;
    // int nvme_ims_close() override;

    int nvme_open_DB(uint32_t& datalen) override;
    int nvme_close_DB(uint8_t* buffer,size_t size) override;


    // int nvme_set_sstable_info(uint32_t *data_len) override;
    // int nvme_set_log_info(uint32_t *data_len) override;

    int nvme_write_metadata(char *buffer,size_t size) override;
    int nvme_read_metadata(char *buffer,size_t size) override;
    
    int nvme_write_log(uint64_t lpn ,char *buffer) override;
    int nvme_read_log(uint64_t lpn ,char *buffer) override;
    int nvme_allcate_lbn(char *buffer) override;
    int nvme_dump_ims() override;
    int nvme_read_ssKeyRange(std::string, char* buffer) override;

    int nvme_write_block(uint32_t lbn, char* buffer) override;
    int nvme_read_block(uint32_t lbn, char* buffer) override;
    int nvme_search(char *buffer,size_t size) override;
    int nvme_read_sstable_page(std::string filename, uint32_t page_off,uint32_t page_nun ,char* buffer) override;
private:
    IMS_interface ims;
};

#endif // __NVME_TEST_HH__