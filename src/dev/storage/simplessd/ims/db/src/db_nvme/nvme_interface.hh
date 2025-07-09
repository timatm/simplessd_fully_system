#include "nvme_config.hh"

int pass_io_command(nmc_config_t *config);

int nvme_write_sstable(sstable_info ,char *buffer);
int nvme_read_sstable(std::string ,char *buffer);
int ims_init();
int ims_close();

int monitor_IMS(int monitor_type);

void init_nmc_config(nmc_config_t *config);
// void print_fd_target(int fd);

int init_device();
int close_device();