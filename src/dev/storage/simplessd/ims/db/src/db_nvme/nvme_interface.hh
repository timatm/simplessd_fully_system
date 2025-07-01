#include "nvme_config.hh"

int pass_io_command(nmc_config_t *config);

int ims_nvme_write(char *buffer);
int ims_nvme_read(char *buffer);
int ims_init();
int monitor_IMS(int monitor_type);

void init_nmc_config(nmc_config_t *config);
// void print_fd_target(int fd);

int init_device();
int close_device();