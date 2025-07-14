#include "slice.hh"

Slice::Slice() : data_(""), size_(0) {}
Slice::Slice(const char* d, size_t n) : data_(d), size_(n) {}
Slice::Slice(const std::string& s) : data_(s.data()), size_(s.size()) {}
Slice::Slice(const char* s) : data_(s), size_(std::strlen(s)) {}

int Slice::compare(const Slice& b) const {
    const size_t min_len = (size_ < b.size_) ? size_ : b.size_;
    int r = std::memcmp(data_, b.data_, min_len);
    if (r == 0) {
        if (size_ < b.size_) r = -1;
        else if (size_ > b.size_) r = +1;
    }
    return r;
}
