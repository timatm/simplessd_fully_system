#ifndef __NVME_SIMPLESSD_HH__
#define __NVME_SIMPLESSD_HH__


#include "nvme_interface.hh"

class gem5Driver : public INVMEDriver {
public:

    int nvme_write_sstable(sstable_info ,char *buffer) override;
    int nvme_read_sstable(std::string ,char *buffer) override;
    int nvme_erase_sstable(std::string) override ;
    int nvme_ims_init() override;
    int nvme_ims_close() override;

    int nvme_open_DB(uint8_t *buffer) override;
    int nvme_close_DB(uint8_t *buffer,size_t size) override;


    // int nvme_set_sstable_info(uint32_t *data_len) override;
    // int nvme_set_log_info(uint32_t *data_len) override;

    int nvme_write_metadata(char *buffer,size_t size) override;
    int nvme_read_metadata(char *buffer,size_t size) override;
    
    int nvme_write_log(uint64_t lpn ,char *buffer) override;
    int nvme_read_log(uint64_t lpn ,char *buffer) override;
    int nvme_allcate_lbn(char *buffer) override;
    int nvme_dump_ims() override;
    int nvme_read_ssKeyRange(std::string, char* buffer) override;
    int nvme_monitor_IMS(int monitor_type);
    int nvme_write_block(uint32_t lbn, char* buffer) override;
    int nvme_read_block(uint32_t lbn, char* buffer) override;
private:
    int pass_io_command(nmc_config_t *config);
    void fill_filename_to_dwords(const std::string& filename, uint32_t* dwords_out);
    void fill_uint64_to_dwords(uint64_t input, uint32_t* dwords_out);
    void init_nmc_config(nmc_config_t *config);
    int init_device();
    int close_device();
};

#endif // __NVME_SIMPLESSD_HH__