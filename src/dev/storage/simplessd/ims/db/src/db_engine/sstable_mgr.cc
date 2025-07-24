#include "sstable_mgr.hh"
#include "def.hh"
#include <sstream>
#include <iomanip>
#include <string>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

static void* allocateAligned(size_t size) {
    void* ptr = nullptr;
    // alignment 必須是 2 的次方，且 >= sizeof(void*)
    if (posix_memalign(&ptr, 4096, size) != 0 || ptr == nullptr) {
        throw std::bad_alloc();
    }
    // 先歸零
    std::memset(ptr, 0, size);
    return ptr;
}

char* SstableManager::keyPerPagePacking(SkipList<Record,RecordComparator> & skiplist) {
    const size_t total_size = IMS_PAGE_NUM * IMS_PAGE_SIZE;
    char* buffer = static_cast<char*>(allocateAligned(total_size));

    auto it = skiplist.GetIterator();
    it.SeekToFirst();

    size_t page = 0;
    while (it.Valid()) {
        InternalKey key = it.record().internal_key;
        std::string  enc = key.Encode();

        if (enc.size() > IMS_PAGE_SIZE) {
            free(buffer);
            throw std::runtime_error("Encoded key size exceeds IMS_PAGE_SIZE");
        }

        // 目標位址：每頁起始 + 已用長度 (這裡一次只放一個 key)
        char* dst = buffer + page * IMS_PAGE_SIZE;
        std::memcpy(dst, enc.data(), enc.size());
        // padding 已在 AllocateAligned 歸零過，無需手動 append

        ++page;
        it.Next();
    }

    if (page != IMS_PAGE_NUM) {
        free(buffer);
        throw std::runtime_error("Packed page count mismatch");
    }
    return buffer;
}

char* SstableManager::keyHashPacking(SkipList<Record,RecordComparator> & skiplist) {
    const size_t slots_per_page = IMS_PAGE_SIZE / sizeof(InternalKey);
    const size_t total_slots    = IMS_PAGE_NUM * slots_per_page;
    const size_t block_size     = total_slots * sizeof(InternalKey);

    char* buffer = static_cast<char*>(allocateAligned(block_size));

    auto it = skiplist.GetIterator();
    it.SeekToFirst();

    while (it.Valid()) {
        const InternalKey& key = it.record().internal_key;
        size_t slot_idx = HashModN(key, slots_per_page);
        bool   placed   = false;

        for (size_t pg = 0; pg < IMS_PAGE_NUM; ++pg) {
            size_t idx    = pg * slots_per_page + slot_idx;
            size_t offset = idx * sizeof(InternalKey);
            InternalKey* ptr = reinterpret_cast<InternalKey*>(buffer + offset);

            if (ptr->key_size == 0) {
                *ptr    = key;
                placed  = true;
                break;
            }
        }

        if (!placed) {
            free(buffer);
            throw std::runtime_error("Hash block full, cannot place key");
        }
        it.Next();
    }
    return buffer;
}

char* keyRangePacking(SkipList<Record, RecordComparator>& skiplist) {
    const size_t slots_per_page = IMS_PAGE_SIZE / sizeof(InternalKey);
    const size_t total_slots = IMS_PAGE_NUM * slots_per_page;
    const size_t block_size = total_slots * sizeof(InternalKey);

    // 配置一個 block_size 的連續空間
    char* buffer = static_cast<char*>(allocateAligned(block_size));
    if (!buffer) {
        throw std::bad_alloc();
    }

    auto iter = skiplist.GetIterator();
    iter.SeekToFirst();

    for (size_t slot = 0; slot < slots_per_page; ++slot) {
        for (size_t page = 0; page < IMS_PAGE_NUM; ++page) {
            if (!iter.Valid()) break;

            size_t flat_index = page * slots_per_page + slot;
            size_t offset = flat_index * sizeof(InternalKey);

            InternalKey* ptr = reinterpret_cast<InternalKey*>(buffer + offset);
            *ptr = iter.record().internal_key;

            iter.Next();
        }
    }

    return buffer;
}

std::string SstableManager::generateFilename(uint32_t seq) {
    constexpr size_t max_digits = sizeof(mappingEntry{}.fileName) - 1;
    std::ostringstream oss;
    oss << std::setw(max_digits) << std::setfill('0') << seq;
    auto str = oss.str();
    assert(str.size() <= max_digits);
    return str;
}

// TODO
void SstableManager::init(){

}
void SstableManager::readSSTable(const std::string& filename){
    
}
void SstableManager::writeSSTable(char * sstable_buffer){
    if (sstable_buffer == nullptr) {
        throw std::invalid_argument("SSTable buffer cannot be null");
    }
    hostInfo request(generateFilename(sequenceNumber_));


}
void SstableManager::deleteSSTable(const std::string& filename){

}