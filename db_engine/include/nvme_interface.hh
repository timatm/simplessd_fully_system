#ifndef __NVME_HH__
#define __NVME_HH__
#include "nvme_config.hh"
#include "IMS_interface.hh"


// class NVMe{
// public:
//     NVMe() = default;
//     int pass_io_command(nmc_config_t *config);

//     int nvme_write_sstable(sstable_info ,char *buffer);
//     int nvme_read_sstable(std::string ,char *buffer);
//     int nvme_ims_init();
//     int nvme_ims_close();

//     int nvme_monitor_IMS(int monitor_type);
//     void init_nmc_config(nmc_config_t *config);
//     // void print_fd_target(int fd);

//     int nvme_open_DB(uint8_t *buffer);
//     int nvme_close_DB();

//     int nvme_write_log(uint64_t lpn ,char *buffer);
//     int nvme_read_log(uint64_t lpn ,char *buffer);
//     int nvme_allcate_lbn(char *buffer);
//     int nvme_dump_ims();
//     int init_device();
//     int close_device();

// private:
//     IMS_interface ims;
// };

class INVMEDriver {
public:
    virtual ~INVMEDriver() = default;

    // virtual int pass_io_command(nmc_config_t *config) = 0;
    virtual int nvme_write_sstable(sstable_info, char* buffer) = 0;
    virtual int nvme_read_sstable(std::string, char* buffer) = 0;
    virtual int nvme_erase_sstable(std::string) = 0;
    virtual int nvme_ims_init() = 0;
    virtual int nvme_ims_close() = 0;
    // virtual int nvme_monitor_IMS(int monitor_type) = 0;
    // virtual void init_nmc_config(nmc_config_t *config) = 0;


    // TODO
    virtual int nvme_write_metadata(char *buffer,size_t size) = 0;
    virtual int nvme_read_metadata(char *buffer,size_t size) = 0;

    // virtual int nvme_set_sstable_info(uint32_t *) = 0;
    // virtual int nvme_set_log_info(uint32_t *) = 0;

    virtual int nvme_open_DB(uint8_t* buffer) = 0;
    virtual int nvme_close_DB(uint8_t* buffer,size_t size) = 0;
    virtual int nvme_write_log(uint64_t lpn, char* buffer) = 0;
    virtual int nvme_read_log(uint64_t lpn, char* buffer) = 0;
    virtual int nvme_allcate_lbn(char* buffer) = 0;
    virtual int nvme_dump_ims() = 0;
    virtual int nvme_read_ssKeyRange(std::string, char* buffer) = 0;

    virtual int nvme_write_block(uint32_t lbn, char* buffer) = 0;
    virtual int nvme_read_block(uint32_t lbn, char* buffer) = 0;
};

#endif