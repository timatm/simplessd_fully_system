#ifndef __PERSISTENCE_CC__
#define __PERSISTENCE_CC__


#include <string>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include "def.hh"
#include "disk.hh"
class Persistence {

public:
    Disk disk;
    int flushMappingTable(std::unordered_map<std::string, uint64_t>&);
    int readMappingTable(uint64_t lbn,uint8_t *buffer,size_t size);
    int flushSStable(uint64_t lbn,uint8_t *buffer,size_t size);
    int readSStable(uint64_t lbn,uint8_t *buffer,size_t size);
    int readSStablePage(uint64_t lpn,uint8_t *buffer,size_t size);
    // TODO log(blob log) need to add 
private:
};
extern Persistence persistenceManager;
#endif // __PERSISTENCE_CC__