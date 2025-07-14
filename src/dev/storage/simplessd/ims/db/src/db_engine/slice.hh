#ifndef LSMDB_SLICE_HH_
#define LSMDB_SLICE_HH_

#include <string>
#include <cstring>

class Slice {
public:
    Slice();
    Slice(const char* d, size_t n);
    Slice(const std::string& s);
    Slice(const char* s);  // NUL-terminated

    const char* data() const { return data_; }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    char operator[](size_t n) const {
        return data_[n];
    }

    void clear() {
        data_ = "";
        size_ = 0;
    }

    void remove_prefix(size_t n) {
        if (n > size_) n = size_;
        data_ += n;
        size_ -= n;
    }

    std::string ToString() const {
        return std::string(data_, size_);
    }

    int compare(const Slice& b) const;

    bool operator==(const Slice& b) const {
        return ((size_ == b.size_) && (std::memcmp(data_, b.data_, size_) == 0));
    }

    bool operator!=(const Slice& b) const {
        return !(*this == b);
    }

private:
    const char* data_;
    size_t size_;
};

#endif  // LSMDB_SLICE_HH_
