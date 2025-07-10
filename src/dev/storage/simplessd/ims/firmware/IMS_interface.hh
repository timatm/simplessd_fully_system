#ifndef __IMS_INTERFACE__HH__
#define  __IMS_INTERFACE__HH__


#include <string>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <cstdint>

#include <vector>
#include <iostream>
#include "def.hh"
extern super_page *sp_ptr_old;
extern super_page *sp_ptr_new;

class IMS_interface{
public:
    int rebuild_super_page();
    int write_sstable(hostInfo *request,uint8_t *buffer);
    int read_sstable(hostInfo *request ,uint8_t *buffer);
    // TODO need to finish
    int search_key(int key);
    int allocate_block(uint64_t *);
    int write_log(uint64_t,uint8_t *buffer);
    int read_log(uint64_t,uint8_t *buffer); 
    int init_IMS();
    int close_IMS();
private:

};



#endif