#ifndef __INTERATOR__HH__
#define __INTERATOR__HH__

#include <string>
#include "status.hh"
#include "record.hh"


class InternalIterator {
public:
    virtual ~InternalIterator() = default;

    virtual Status Init() = 0;
    virtual bool Valid() const = 0;

    // 定位到第一個 ≥ lower 的 key
    virtual void SeekToFirst() = 0;

    // 定位到最後一個 < upper 的 key
    virtual void SeekToLast()  = 0;

    // 定位到第一個 ≥ target 的 key
    virtual void Seek(std::string_view internal_target) = 0;

    virtual void Next() = 0;
    virtual void Prev() = 0;

    virtual std::string_view key()   const = 0;  // internal key (encoded)

    virtual Status status() const = 0;
    virtual bool SupportsValueView() const { return false; }
    virtual std::optional<std::string_view> value_view() const { return std::nullopt; }

    virtual bool SupportsValueCopy() const { return false; }
    virtual Status ReadValue(std::string& /*out*/) const {
        return Status::NotSupported("iterator does not support value");
    }
};

#endif