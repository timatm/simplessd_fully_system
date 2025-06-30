#ifndef __NMC_HOST_PLUGIN_NVME_NMC_H__
#define __NMC_HOST_PLUGIN_NVME_NMC_H__

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    // host info
    struct
    {
        bool dry;
        char *data_file;
        int fd;
        uint8_t flags;
        uint16_t rsvd;
        uint32_t result;
        uint32_t timeout_ms;

        char *data;
        uint32_t data_len;
        char *metadata;
        uint32_t metadata_len;
        uint8_t monitor_type;
    };

    // device (NVMe) info
    struct
    {
        union
        {
            struct
            {
                uint8_t OPCODE;
                uint8_t FUSE : 2;      // fuse operation
                uint8_t reserved0 : 4; //
                uint8_t PSDT : 2;      // 00 for PRP, 11 reserved, otherwise SGL.
                uint16_t CID;          // command id
            };
            uint32_t cdw00; // CDW 0: command info
        };

        union
        {
            uint32_t NSID;
            uint32_t cdw01; // CDW 1: namespace id
        };

        uint32_t cdw02; // command specific
        uint32_t cdw03; // command specific

        union
        {
            uint64_t meta_addr;
            struct
            {
                uint32_t cdw04;
                uint32_t cdw05;
            }; // CDW 4,5: dword aligned physical address of the metadata buffer
        };

        union
        {
            // struct SGL; // not supported yet
            struct
            {
                uint64_t PRP1; // physical address of the data buffer or PRP list
                uint64_t PRP2;
            };
            struct
            {
                uint32_t cdw06;
                uint32_t cdw07;
                uint32_t cdw08;
                uint32_t cdw09;
            };
        }; // CDW 6:9: data buffer info

        union
        {
            struct
            {
                union
                {
                    uint32_t cdw10;        // command specific
                };
                uint32_t cdw11; // command specific
            };
            uint64_t slba; // the starting LBA
        };

        union
        {
            uint32_t cdw12; // command specific
            struct
            {
                uint16_t nlb; // number of logical blocks
                uint16_t unused;
            };
        };

        uint32_t cdw13; // command specific
        union
        {
            uint32_t cdw14; // command specific
        };
        union
        {
            uint32_t cdw15; // command specific
        };
    };
} nmc_config_t;

/* -------------------------------------------------------------------------- */
/*                             NMC related configs                            */
/* -------------------------------------------------------------------------- */

#define OPCODE_WRITE_SSTABLE    0x80
#define OPCODE_READ_SSTABLE     0x81
#define OPCODE_SEARCH_KEY       0x82
#define OPCODE_INIT_IMS         0x83
#define OPCODE_MONITOR_IMS      0x84

typedef enum{
  DUMP_MAPPING_INFO,
  DUMP_LBNPOOL_INFO,
} MONITOR_TYPE;


typedef enum{
    STATUS_OPERATION_SUCCESS,
    STATUS_LBN_INVALID = 0x90,
    STATUS_IMS_INIT_FAILED,
    STATUS_MONITOR_FAILD,
    STATUS_WRITE_SSTABLE_FAILD
} STATUS_CODE;

#define COMMAND_SUCCESS     0
#define COMMAND_FAILD       1
/* -------------------------------------------------------------------------- */
/*                              public interfaces                             */
/* -------------------------------------------------------------------------- */

#define PAGE_SIZE 16384
#endif /* __NMC_HOST_PLUGIN_NVME_NMC_H__ */