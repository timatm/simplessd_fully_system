#ifndef __MAPPING_TABLE_HH__
#define __MAPPING_TABLE_HH__
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <cstdint>

#include <vector>
#include <iostream>
#include "def.hh"

#include "persistence.hh"
#include "lbn_pool.hh"
#include "tree.hh"

// class LBNPool;   
// class Mapping{
//     LBNPool& lbnPoolManager;
// public:
//     Mapping(LBNPool& p) : lbnPoolManager(p) {}
//     std::unordered_map<std::string, uint64_t> mappingTable;
//     int init_mapping_table(uint64_t mappingPageLBN,uint64_t page_num);

//     void insert_mapping(const std::string& filename, uint64_t lbn);

//     uint64_t getLBN(const std::string& filenam);

//     void remove_mapping(const std::string& filename);
    
//     void dump_mapping();

//     int flush_mapping_table();
    
//     void clear();
// };


class Mapping {
public:
    Mapping(Persistence& persistence, LBNPool& pool, Tree& tree);


    int init_mapping_table(uint64_t mappingPageLBN, uint64_t page_num);

    void insert_mapping(const std::string& filename, uint64_t lbn);
    uint64_t getLBN(const std::string& filename) const;
    void remove_mapping(const std::string& filename);
    void dump_mapping() const;
    void clear();

    const std::unordered_map<std::string, uint64_t>& get_table() const {
        return mappingTable_;
    }

    std::unordered_map<std::string, uint64_t>& get_table_mutable() {
        return mappingTable_;
    }

private:
    std::unordered_map<std::string, uint64_t> mappingTable_;
    Persistence& persistenceManager_;
    LBNPool& lbnPool_;
    Tree& tree_;

};

#endif // __MAPPING_TABLE_HH__