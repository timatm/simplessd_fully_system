#ifndef __NVME_HH__
#define __NVME_HH__
#include "nvme_config.hh"
#include "IMS_interface.hh"


class NVMe{
public:
    NVMe(){
        ims.init_IMS();
    }
    int pass_io_command(nmc_config_t *config);

    int nvme_write_sstable(sstable_info ,char *buffer);
    int nvme_read_sstable(std::string ,char *buffer);
    int nvme_ims_init();
    int nvme_ims_close();

    int nvme_monitor_IMS(int monitor_type);
    void init_nmc_config(nmc_config_t *config);
    // void print_fd_target(int fd);


    int nvme_write_log(uint64_t lpn ,char *buffer);
    int nvme_read_log(uint64_t lpn ,char *buffer);
    int nvme_allcate_lbn(char *buffer);
    int init_device();
    int close_device();

private:
    IMS_interface ims;
};


#endif