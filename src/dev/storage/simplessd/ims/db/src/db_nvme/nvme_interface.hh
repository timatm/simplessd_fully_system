#include "nvme_config.hh"

int ims_nvme_write(nmc_config_t *config,int fd);
int ims_nvme_read(nmc_config_t *config,int fd);
void print_fd_target(int fd);
void init_nmc_config(nmc_config_t *config);
int ims_init(nmc_config_t *config ,int fd);
int pass_io_command(nmc_config_t *config,int fd);
