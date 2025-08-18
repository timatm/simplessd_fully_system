#ifndef __INTERATOR__HH__
#define __INTERATOR__HH__

#include <string>
#include "status.hh"
#include "record.hh"


class InternalIterator {
public:
    virtual ~InternalIterator() = default;

    virtual bool Valid() const = 0;
    virtual void SeekToFirst() = 0;
    virtual void SeekToLast()  = 0;

    // target 是 "internal key 的編碼" 視圖（user_key + seq/type）
    virtual void Seek(std::string_view internal_target) = 0;

    virtual void Next() = 0;
    virtual void Prev() = 0;

    virtual std::string_view key()   const = 0;  // internal key (encoded)

    virtual Status status() const = 0;
    virtual bool SupportsValueView() const { return false; }
    virtual std::optional<std::string_view> value_view() const { return std::nullopt; }

    // 可选能力：拥有权拷贝
    virtual bool SupportsValueCopy() const { return false; }
    virtual Status ReadValue(std::string& /*out*/) const {
        return Status::NotSupported("iterator does not support value");
    }
};

#endif