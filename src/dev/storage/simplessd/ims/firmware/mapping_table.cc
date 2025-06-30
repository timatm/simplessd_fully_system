#include "mapping_table.hh"
#include <iomanip>
#include <algorithm>

#include "persistence.hh"
#include "tree.hh"
#include "lbn_pool.hh"
int Mapping::init_mapping_table(uint64_t mappingPageLBN,uint64_t page_num){
    int err = OPERATION_FAILURE;
    if(mappingPageLBN == INVALIDLBN) {
        pr_info("Invalid mapping page LBN, cannot initialize mapping table");
        return err;
    }
    size_t size = IMS_PAGE_SIZE;
    uint8_t *buffer  = (uint8_t*)malloc(size);
    if (!buffer) {
        pr_info("Failed to allocate buffer for mapping table");
        return err;
    }
    uint64_t lpn = LBN2LPN(mappingPageLBN);
    for(int page = 0;page < page_num;page++){
        err = persistenceManager.readMappingTable(lpn, buffer, size);
        if(err != OPERATION_SUCCESS){
            pr_info("Failed to read mapping table at LPN: %lu", lpn);
            free(buffer);
            return OPERATION_FAILURE;
        }
        mappingTablePerPage *mappingTablePtr = (mappingTablePerPage *)buffer;
        if(mappingTablePtr->entry_num > MAPPING_TABLE_ENTRIES){
            pr_info("Mapping table entry num is error: %d",mappingTablePtr->entry_num);
        }
        for (int i = 0; i < mappingTablePtr->entry_num; i++) {
            mappingEntry *entry = &mappingTablePtr->entry[i];

            // Recover mapping table and SStable tree info
            if (entry->lbn != INVALIDLBN) {
                mappingTable[std::string(entry->fileName)] = entry->lbn;
                std::shared_ptr<TreeNode> node = std::make_shared<TreeNode>(entry->fileName,entry->level, entry->channel, entry->minRange, entry->maxRange);
                tree.insert_node(node);
            }
        }
        lpn++;
    }
    free(buffer);
    return OPERATION_SUCCESS;
}


void Mapping::insert_mapping(const std::string& filename, uint64_t lbn) {
    if (mappingTable.find(filename) != mappingTable.end()) {
        std::cerr << "File already exists in the mapping table , update mapping to " << lbn << "\n";
    }
    auto it = std::find(lbnPoolManager.freeLBNList[LBN2CH(lbn)].begin(),lbnPoolManager.freeLBNList[LBN2CH(lbn)].end(),lbn);
    if(it == lbnPoolManager.freeLBNList[LBN2CH(lbn)].end()){
        pr_info("Free list does't have LBN:%lld" ,lbn);
    }
    lbnPoolManager.remove_freeLBNList(lbn);
    mappingTable[filename] = lbn;
    lbnPoolManager.insert_usedLBNList(lbn);
    return;
}

uint64_t Mapping::getLBN(const std::string& filename){
    return mappingTable[filename];
}

void Mapping::remove_mapping(const std::string& filename) {
    auto it = mappingTable.find(filename);
    if (it == mappingTable.end()) {
        pr_info("File \"%s\" does not exist in the mapping table", filename.c_str());
        return; 
    }
    uint64_t lbn = it->second;
    mappingTable.erase(it);
    lbnPoolManager.remove_usedLBNList(lbn);   
    lbnPoolManager.insert_freeLBNList(lbn);    
}


void Mapping::dump_mapping(mappingTablePerPage *page) {
    pr_info("Dumping mapping table to page...");
    pr_info("===== Mapping Table Page =====");
    size_t count = 0;
    for (size_t i = 0; i < page->entry_num; i++) {
        mappingEntry *entry = &page->entry[i];
        pr_info("Entry %zu: FileName: %s -> LBN: %lu", count, entry->fileName, entry->lbn);
    }
    pr_info("================================");
}


int Mapping::flush_mapping_table(){
    
}

void Mapping::clear(){
    mappingTable.clear();
    return;
}