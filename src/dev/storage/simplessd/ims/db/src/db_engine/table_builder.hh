#ifndef __TABLE_BUILDER_HH_
#define __TABLE_BUILDER_HH_

#include <cstddef>
#include <cstdint>
#include <string>
class TableBuilder {
public:
    TableBuilder() = default;
    virtual ~TableBuilder() = default;

    void reset();
    void buildTable();
    
private:
    std::string buffer_;
    uint64_t lbn_;
    uint8_t level_;
    uint8_t channel_;
    uint32_t minRange_;
    uint32_t maxRange_;
};
#endif  // TABLE_HH_