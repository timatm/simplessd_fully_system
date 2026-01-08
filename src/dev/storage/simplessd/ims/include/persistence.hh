#ifndef __PERSISTENCE_CC__
#define __PERSISTENCE_CC__

#include <memory>
#include "tree.hh"
#include "def.hh"
#include "disk.hh"
#if RUNTYPE
#include "util/disk.hh"
#endif
#if RUNTYPE
namespace SimpleSSD {
    class Disk; 
}
#else
class Disk;         // 你的 ims/disk.hh 那個 Disk，可以 forward 或 include
#endif


class Persistence {
public:
#if RUNTYPE
    SimpleSSD::Disk* pDisk_ = nullptr;
#else
    Disk* pDisk_ = nullptr;
#endif
#if RUNTYPE
    Persistence(SimpleSSD::Disk* disk, super_page* sp_, Tree& tree)
        : pDisk_(disk), sp_ptr_(sp_), tree_(tree) {}
#else
    Persistence(Disk* disk, super_page* sp_ , Tree& tree)
        : pDisk_(disk), sp_ptr_(sp_), tree_(tree) {}
#endif
    int flushMappingTable(const std::unordered_map<std::string, uint64_t>& mappingTable);
    int readMappingTable(uint64_t lpn, uint8_t* buffer, size_t size);
    int writeBlock(uint64_t lbn, uint8_t* buffer, size_t size);
    int eraseBlock(uint64_t lbn);
    int readBlock(uint64_t lbn, uint8_t* buffer, size_t size);
    // int readSStablePage(uint64_t lpn, uint8_t* buffer, size_t size);
    int readPage(uint64_t lpn, uint8_t* buffer, size_t size);
    int writePage(uint64_t lpn, uint8_t* buffer, size_t size);

private:

    super_page* sp_ptr_ = nullptr;
    Tree& tree_;
};

#endif // __PERSISTENCE_CC__