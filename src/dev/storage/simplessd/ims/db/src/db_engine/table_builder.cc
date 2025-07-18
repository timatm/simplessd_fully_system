#include "table_builder.hh"




void TableBuilder::reset() {
    buffer_.clear();
    lbn_ = 0;
    level_ = 0;
    channel_ = 0;
    minRange_ = 0;
    maxRange_ = 0;
} 