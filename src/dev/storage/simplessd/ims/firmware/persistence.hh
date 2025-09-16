#ifndef __PERSISTENCE_CC__
#define __PERSISTENCE_CC__

#include <memory>
#include "tree.hh"
#include "def.hh"
#include "disk.hh"

class Persistence {
public:
#if RUNTYPE_SIMPLESSD
    SimpleSSD::Disk* pDisk_ = nullptr;
#else
    Disk* pDisk_ = nullptr;
#endif

#if RUNTYPE_SIMPLESSD
    Persistence(SimpleSSD::Disk* disk, super_page* sp_old, super_page* sp_new, Tree& tree)
        : pDisk_(disk), sp_ptr_old_(sp_old), sp_ptr_new_(sp_new), tree_(tree) {}
#else
    Persistence(Disk* disk, super_page* sp_old, super_page* sp_new, Tree& tree)
        : pDisk_(disk), sp_ptr_old_(sp_old), sp_ptr_new_(sp_new), tree_(tree) {}
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

    super_page* sp_ptr_old_ = nullptr;
    super_page* sp_ptr_new_ = nullptr;
    Tree& tree_;
};

#endif // __PERSISTENCE_CC__