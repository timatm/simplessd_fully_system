#ifndef MEMTABLE_HH_
#define MEMTABLE_HH_

#include <string>
#include <optional>
#include <vector>
#include "internal_key.hh"
#include "record.hh"
#include "skiplist.hh"
#include "def.hh"
#include "options.hh"
#include "iterator.hh"



class MemTable {
public:
    MemTable();

    void Put(const Record &rec);
    std::optional<std::string> Get(const std::string& user_key) const;
    std::optional<Record> get_record(const std::string& user_key) const;
    void Dump() const;
    size_t ApproximateMemoryUsage() const;
    bool memTableIsFull();
    const SkipList<Record, RecordComparator>& GetSkipList() const { return skiplist_; }

    InternalKey getMinKey(){ return skiplist_.Min()->internal_key ;};
    InternalKey getMaxKey(){ return skiplist_.Max()->internal_key ;};
    void setPackingT(PackingType t){packing_type_ = static_cast<int>(t); }
    bool isEmpty() const { return node_count_ == 0; }
private:
    SkipList<Record,RecordComparator> skiplist_;  // SkipList 儲存 Record
    uint32_t node_count_ = 0;              // SkipList 節點數
    size_t size_ = 0;                      // 估計記憶體大小
    int packing_type_ = static_cast<int>(PACKING_T);
    std::vector<uint32_t> hash_num_;
    InternalKey minRange_;
    InternalKey maxRange_;
};



class MemTableIterator : public InternalIterator {
public:
    MemTableIterator(const std::shared_ptr<SkipList<Record, RecordComparator>>& list,
                     const InternalKeyComparator* icmp)
    : list_(list.get()), holder_(list), it_(list_->GetIterator()), icmp_(icmp) {
        key_buf_.clear();
        value_buf_ = {};
    }

    MemTableIterator(const SkipList<Record, RecordComparator>& list,
                     const InternalKeyComparator* icmp)
    : list_(&list), it_(list_->GetIterator()), icmp_(icmp) {
        key_buf_.clear();
        value_buf_ = {};
    }

    Status Init() override;
    bool Valid() const override;

    void SeekToFirst() override;
    void SeekToLast()  override;
    void Seek(std::string_view internal_target) override;

    void Next() override;
    void Prev() override;

    std::string_view key()   const override { return std::string_view(key_buf_); }
    bool SupportsValueView() const override { return true; }
    std::optional<std::string_view> value_view() const override{
        return value_buf_.empty() ? std::nullopt : std::make_optional(value_buf_);
    }
    
    bool SupportsValueCopy() const override { return true; }
    Status ReadValue(std::string& /*out*/) const override ;


    Status status() const override { return status_; }

private:
    void SyncKV();
    const SkipList<Record, RecordComparator>* list_{nullptr};
    std::shared_ptr<SkipList<Record, RecordComparator>> holder_;

    typename SkipList<Record, RecordComparator>::Iterator it_;
    const InternalKeyComparator* icmp_{nullptr};

    std::string      key_buf_;
    std::string_view value_buf_;
    Status           status_{Status::OK()};
};




size_t HashModN(const InternalKey& ikey, size_t n);

#endif