#ifndef __RANGE__QUERY__HH__
#define __RANGE__QUERY__HH__

#include <vector>
#include <queue>
#include <optional>
#include <string>
#include <string_view>
#include <memory>
#include <cstddef>

#include "memtable.hh"
#include "level_iter.hh"  // 假設這裡面提供 InternalIterator/Status/Options/Comparators 等宣告

class QueryIterator : public InternalIterator {
public:
    QueryIterator(SstableManager* smgr,
                  LogManager* lmgr,
                  LSMTree* tree,
                  const InternalKeyComparator* icmp,
                  std::unique_ptr<MemTableIterator> memIter,
                  std::unique_ptr<MemTableIterator> immuteIter)
        : smgr_(smgr),
          lmgr_(lmgr),
          tree_(tree),
          icmp_(icmp),
          heap_(HeapCmp{icmp_, &children_}), 
          memIter_(std::move(memIter)),
          immuteIter_(std::move(immuteIter)){}

    ~QueryIterator() override = default;

    // 設定半開區間 [lower, upper)；以 InternalKey 的編碼字節作為邊界
    void SetInternalRange(std::optional<std::string> lower,
                          std::optional<std::string> upper);
    // InternalIterator 介面
    Status Init() override;
    bool   Valid() const override;

    void   SeekToFirst() override;
    void   SeekToLast()  override;              // 目前回 NotSupported
    void   Seek(std::string_view target) override;

    void   Next() override;
    void   Prev() override;                     // 目前回 NotSupported

    std::string_view key() const override;

    Status status() const override { return st_; }


    // bool SupportsValueCopy() const override {
    //     if (!Valid()) return false;
    //     auto* it = children_[curr_idx_].it.get();
    //     return it && it->SupportsValueCopy();
    // }
    bool SupportsValueCopy() const override {return true;}
    Status ReadValue(std::string& /*out*/) const override ;

public:
    struct Child {
        std::unique_ptr<InternalIterator> it;
        bool in_heap{false};
    };

    struct HeapCmp {
        const InternalKeyComparator* icmp;
        const std::vector<Child>*    children;

        bool operator()(size_t a, size_t b) const;
        static bool LessKey(const InternalKeyComparator& ic,
                            std::string_view a, std::string_view b);
    };
    using Heap = std::priority_queue<size_t, std::vector<size_t>, HeapCmp>;

private:
    // 依賴（非擁有；由呼叫端保證生命週期覆蓋本迭代器）
    SstableManager* smgr_{nullptr};
    LogManager*     lmgr_{nullptr};
    LSMTree*        tree_{nullptr};
    const InternalKeyComparator* icmp_{nullptr};
    Options         opts_{};

    // 可選：如果你先在外部建好這些 iter，會在 BuildChildren_() 中 move 進 children_
    std::unique_ptr<MemTableIterator>   memIter_;
    std::unique_ptr<MemTableIterator>   immuteIter_;

    // 以 InternalKey 編碼作為邊界（半開區間 [lower, upper)）
    std::optional<std::string> canon_lower_;
    std::optional<std::string> canon_upper_;

    // k-way merge 的孩子集合 + 小根堆
    std::vector<Child> children_;
    Heap               heap_{HeapCmp{nullptr, nullptr}};

    bool               has_top_{false};
    std::string_view   key_;
    size_t             curr_idx_{static_cast<size_t>(-1)};
    Status             st_{Status::OK()};
    
private:
    // 私有輔助：統一建/收集 children，初始化堆等
    Status BuildChildren_();
    void   SeekRebuildHeap_(std::string_view target);
    void   PushIfValid_(size_t idx);
    void   PopAndAdvanceTop_();
    void   build_iter(int level);
};

#endif  // __RANGE__QUERY__HH__
