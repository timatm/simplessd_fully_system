#include "sstable_mgr.hh"
#include "def.hh"
#include "internal_key.hh"
#include "nvme_interface.hh"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

std::string SstableManager::packingTable(const SkipList<Record, RecordComparator>& skiplist){
    std::string package;
    switch (packing_type_)
    {
        case 0:
            package = std::string(keyPerPagePacking(skiplist), BLOCK_SIZE);
            break;
        case 1:
            package = std::string(keyHashPacking(skiplist),BLOCK_SIZE);
            break;
        case 2:
            package = std::string(keyRangePacking(skiplist),BLOCK_SIZE);
            break;
        default:
            break;
    }
    return package;
}

static void* allocateAligned(size_t size) {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, 4096, size) != 0 || ptr == nullptr) {
        throw std::bad_alloc();
    }
    std::memset(ptr, 0, size);
    return ptr;
}

// forward declare HashModN (assume in another file)
size_t HashModN(const InternalKey& ikey, size_t n) {
    std::string_view data = ikey.Encode(); 

    std::hash<std::string_view> hasher;
    size_t hash_value = hasher(data);
    return hash_value % n;
}

char* SstableManager::keyPerPagePacking(const SkipList<Record, RecordComparator>& skiplist) {
    const size_t total_size = IMS_PAGE_NUM * IMS_PAGE_SIZE;
    std::cout << skiplist.get_node_num() << std::endl;
    char* buffer = static_cast<char*>(allocateAligned(total_size));
    if (!buffer) throw std::runtime_error("Failed to allocate aligned buffer");

    std::memset(buffer, 0xFF, total_size);  // optional: fill with default value

    auto it = skiplist.GetIterator();
    it.SeekToFirst();

    size_t page = 0;
    while (it.Valid()) {
        if (page > IMS_PAGE_NUM) {
            free(buffer);
            throw std::runtime_error("Too many records for fixed page count (IMS_PAGE_NUM)");
        }

        InternalKey key = it.record().internal_key;
        std::string enc = key.Encode();

        if (enc.size() > IMS_PAGE_SIZE) {
            free(buffer);
            throw std::runtime_error("Encoded key size exceeds IMS_PAGE_SIZE");
        }

        char* dst = buffer + page * IMS_PAGE_SIZE;
        std::memcpy(dst, enc.data(), enc.size());

        ++page;
        it.Next();
    }

    return buffer;
}



char* SstableManager::keyHashPacking(const SkipList<Record, RecordComparator>& skiplist) {
    const size_t slots_per_page = IMS_PAGE_SIZE / sizeof(InternalKey);
    const size_t total_slots = IMS_PAGE_NUM * slots_per_page;
    const size_t block_size = total_slots * sizeof(InternalKey);

    char* buffer = static_cast<char*>(allocateAligned(block_size));
    auto it = skiplist.GetIterator();
    it.SeekToFirst();

    while (it.Valid()) {
        const InternalKey& key = it.record().internal_key;
        size_t slot_idx = HashModN(key, slots_per_page);
        bool placed = false;

        for (size_t pg = 0; pg < IMS_PAGE_NUM; ++pg) {
            size_t idx = pg * slots_per_page + slot_idx;
            size_t offset = idx * sizeof(InternalKey);
            InternalKey* ptr = reinterpret_cast<InternalKey*>(buffer + offset);
            if (ptr->key.key_size == 0) {
                *ptr = key;
                placed = true;
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

char* SstableManager::keyRangePacking(const SkipList<Record, RecordComparator>& skiplist) {
    const size_t slots_per_page = IMS_PAGE_SIZE / sizeof(InternalKey);
    const size_t total_slots = IMS_PAGE_NUM * slots_per_page;
    const size_t block_size = total_slots * sizeof(InternalKey);

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

void SstableManager::init() {}

void SstableManager::readSSTable(const std::string& filename,char *buffer) {
    
    if (!buffer) {
        std::cerr << "Failed to allocate buffer for reading SSTable\n";
        return;
    }

    thread_pool_.Submit([filename, buffer, this]() {
        std::cout << "Reading SSTable from: " << filename << std::endl;

        int err = nvme_.nvme_read_sstable(filename, buffer);
        if (err == COMMAND_FAILD) {
            std::cerr << "[Thread] Failed to read SSTable: " << filename << std::endl;
            std::free(buffer);
            return;
        }
        std::cout << "[Thread] Read success: " << filename << std::endl;
    });

    std::cout << "[Main] Async read dispatched.\n";
}


void SstableManager::writeSSTable(uint8_t level, InternalKey minKey, InternalKey maxKey, std::string sstable_buffer) {
    if (sstable_buffer.empty()) {
        std::cerr << "SSTable buffer cannot be null" << std::endl;
        return;
    }

    Key rangeMinKey = minKey.key;
    Key rangeMaxKey = maxKey.key;
    std::string filename = generateFilename(sequenceNumber_.fetch_add(1));

    sstable_info info(filename, level, rangeMinKey, rangeMaxKey);
    std::cout << "Dispatching write for SSTable: " << filename << std::endl;
    info.dump();

    thread_pool_.Submit([info, buf = std::move(sstable_buffer), this]() {
        std::cout << "[Thread] Entered thread task\n";
        int err = nvme_.nvme_write_sstable(info,  const_cast<char*>(buf.data()));
        std::cout << "[Thread] nvme_write_sstable returned " << err << std::endl;
        if (err == COMMAND_FAILD) {
            std::cerr << "[Thread] Failed to write SSTable: " << info.filename << std::endl;
            return;
        }
        std::cout << "[Thread] Write success: " << info.filename << std::endl;

        auto node = std::make_shared<TreeNode>(info.filename,
                                            info.level,
                                            info.min, 
                                            info.max);
        {
            std::unique_lock<std::mutex> lock(tree_mutex_);
            lsmTree_.insert_sstable(node);
        }
        

        std::cout << "SStable(" << info.filename << ") written successfully.\n";
    });

    std::cout << "[Main] Async write dispatched.\n";
}


void SstableManager::deleteSSTable(const std::string& filename) {}