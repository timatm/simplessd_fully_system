#include "mapping_table.hh"
#include "tree.hh"
#include <iomanip>
#include <algorithm>
#include <memory>
#include "print.hh"

Mapping::Mapping(Persistence& persistence, LBNPool& pool, Tree& tree)
    : persistenceManager_(persistence), lbnPool_(pool), tree_(tree) {
}

int Mapping::init_mapping_table(uint64_t mappingPageLBN, uint64_t page_num) {
    int err = OPERATION_FAILURE;
    if (mappingPageLBN == INVALIDLBN) {
        pr_error("Invalid mapping page LBN, cannot initialize mapping table");
        return err;
    }

    size_t size = IMS_PAGE_SIZE;
    uint8_t* buffer = (uint8_t*)malloc(size);
    if (!buffer) {
        pr_error("Failed to allocate buffer for mapping table");
        return err;
    }

    uint64_t lpn = LBN2LPN(mappingPageLBN);
    pr_info("Initializing mapping table from LPN: %lu with page num: %lu", lpn, page_num);

    for (int page = 0; page < page_num; page++) {
        err = persistenceManager_.readMappingTable(lpn, buffer, size);
        if (err != OPERATION_SUCCESS) {
            pr_error("Failed to read mapping table at LPN: %lu", lpn);
            free(buffer);
            return OPERATION_FAILURE;
        }

        mappingTablePerPage* mappingTablePtr = (mappingTablePerPage*)buffer;
        if (mappingTablePtr->entry_num > MAPPING_TABLE_ENTRIES) {
            pr_error("Mapping table entry num is error: %d", mappingTablePtr->entry_num);
        }

        pr_info("Mapping table[%d] entry num: %d", page, mappingTablePtr->entry_num);
        for (int i = 0; i < mappingTablePtr->entry_num; i++) {
            mappingEntry* entry = &mappingTablePtr->entry[i];
            if (entry->lbn != INVALIDLBN) {
                pr_info("Recover mapping entry: filename: %s, lbn: %lu, level: %d, channel: %d, range: [%s, %s]",
                        entry->fileName, entry->lbn, entry->level, entry->channel, entry->minRange.toString(), entry->maxRange.toString());
                mappingTable_[entry->fileName] = entry->lbn;

                auto node = std::make_shared<TreeNode>(entry->fileName, entry->level, entry->channel, entry->minRange, entry->maxRange);
                tree_.insert_node(node);
            }
        }
        lpn++;
    }

    free(buffer);
    return OPERATION_SUCCESS;
}

void Mapping::insert_mapping(const std::string& filename, uint64_t lbn) {
    if (mappingTable_.find(filename) != mappingTable_.end()) {
        std::cerr << "File already exists in the mapping table, updating to LBN: " << lbn << "\n";
    }
    auto& list = lbnPool_.get_freeLBNList_ref(LBN2CH(lbn)); 
    auto it = std::find(list.begin(), list.end(), lbn);
    if (it == list.end()) {
        pr_error("Free list does not have LBN: %llu(CH:%d)", lbn,LBN2CH(lbn));
        return;
    }

    lbnPool_.remove_freeLBNList(lbn);
    mappingTable_[filename] = lbn;
    lbnPool_.insert_usedLBNList(lbn);
}

uint64_t Mapping::getLBN(const std::string& filename) const {
    auto it = mappingTable_.find(filename);
    return (it != mappingTable_.end()) ? it->second : INVALIDLBN;
}

void Mapping::remove_mapping(const std::string& filename) {
    auto it = mappingTable_.find(filename);
    if (it == mappingTable_.end()) {
        pr_error("File \"%s\" does not exist in the mapping table", filename.c_str());
        return;
    }

    uint64_t lbn = it->second;
    mappingTable_.erase(it);
    lbnPool_.remove_usedLBNList(lbn);
    lbnPool_.insert_freeLBNList(lbn);
}

void Mapping::dump_mapping() const {
    pr_info("[Mapping Table Dump]");
    for (const auto& [filename, lbn] : mappingTable_) {
        pr_info("SStable ID: %s -> LBN(%lu)", filename.c_str(), lbn);
    }
    pr_info("================================");
}

int Mapping::flush_mapping_table() {
    // TODO：根據 mappingTable_ 分頁序列化成 mappingTablePerPage 結構後寫入
    // 你可以將其對應的 LPN 為 sp_ptr_new_->mapping_store 開始的連續區塊
    return OPERATION_SUCCESS;
}

void Mapping::clear() {
    mappingTable_.clear();
}
