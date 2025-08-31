#include "range_query.hh"
#include <cassert>
#include <cstring>


static constexpr size_t kIKeySize = sizeof(InternalKey);

// ------------- 小工具：上界檢查（key < upper 才在區間內） -------------
namespace {
inline bool InUpperBound(const InternalKeyComparator& ic,
                         std::string_view key,
                         const std::optional<std::string>& upper) {
    if (!upper) return true; // 無上界
    return QueryIterator::HeapCmp::LessKey(ic, key, *upper); // key < upper
}
} // namespace

// ------------- HeapCmp 實作 -------------



bool QueryIterator::HeapCmp::LessKey(const InternalKeyComparator& ic,
                                           std::string_view a, std::string_view b) {
    // assert(a.size() == kIKeySize && b.size() == kIKeySize);
    InternalKey ia, ib;
    std::memcpy(&ia, a.data(), kIKeySize);
    std::memcpy(&ib, b.data(), kIKeySize);
    return ic(ia, ib);
}

bool QueryIterator::HeapCmp::operator()(size_t a, size_t b) const {
    // priority_queue 的 Compare 應回傳「a 的優先級是否低於 b」
    // 若要最小堆（最小 key 在 top），則在 a > b 時回傳 true
    const auto& ca = (*children)[a];
    const auto& cb = (*children)[b];

    assert(ca.it && cb.it);
    assert(ca.it->Valid() && cb.it->Valid());

    const std::string_view ka = ca.it->key();
    const std::string_view kb = cb.it->key();

    // a > b 等價於 LessKey(kb, ka)
    return LessKey(*icmp, kb, ka);
}

// ------------- QueryIterator 私有方法 -------------

Status QueryIterator::BuildChildren_() {
    children_.clear();
    
    // 如果你也想把 memtable/immutable 納入，這裡可以先 push 進 children_
    if (memIter_) {
        children_.push_back(Child{std::move(memIter_), false});
    }
    if (immuteIter_) {
        children_.push_back(Child{std::move(immuteIter_), false});
    }

    // 將各 level 的 iterator 建好後「push 進 children_」
    for (int level = 0; level < MAX_LEVEL; level++) {
        if (tree_->get_level_num(level) > 0) {
            build_iter(level);  // 這個函式內部會 push_back 到 children_
        }
    }

    return Status::OK();
}

void QueryIterator::build_iter(int level) {
    switch (level) {
        case 0: {
            auto it = std::make_unique<Level0Iterator>(smgr_, lmgr_, icmp_, tree_, opts_,false);
            children_.push_back(Child{std::move(it), false});
            return;
        }
        case 1: {
            auto it = std::make_unique<LevelNIterator>(smgr_, lmgr_, icmp_, tree_, 1, opts_);
            children_.push_back(Child{std::move(it), false});
            return;
        }
        case 2: {
            auto it = std::make_unique<LevelNIterator>(smgr_, lmgr_, icmp_, tree_, 2, opts_);
            it->Init();
            children_.push_back(Child{std::move(it), false});
            return;
        }
        case 3: {
            auto it = std::make_unique<LevelNIterator>(smgr_, lmgr_, icmp_, tree_, 3, opts_);
            it->Init();
            children_.push_back(Child{std::move(it), false});
            return;
        }
        case 4: {
            auto it = std::make_unique<LevelNIterator>(smgr_, lmgr_, icmp_, tree_, 4, opts_);
            it->Init();
            children_.push_back(Child{std::move(it), false});
            return;
        }
        case 5: {
            auto it = std::make_unique<LevelNIterator>(smgr_, lmgr_, icmp_, tree_, 5, opts_);
            children_.push_back(Child{std::move(it), false});
            return;
        }
        case 6: {
            auto it = std::make_unique<LevelNIterator>(smgr_, lmgr_, icmp_, tree_, 6, opts_);
            children_.push_back(Child{std::move(it), false});
            return;
        }
        default:
            return;
    }
}



void QueryIterator::SeekRebuildHeap_(std::string_view target) {
    while (!heap_.empty()) heap_.pop();

    for (size_t i = 0; i < children_.size(); ++i) {
        auto& ch = children_[i];
        if (!ch.it) continue;

        ch.it->Seek(target);
        ch.in_heap = false;

        if (!ch.it->Valid()) continue;
        if (!InUpperBound(*icmp_, ch.it->key(), canon_upper_)) continue;

        heap_.push(i);
        ch.in_heap = true;
    }

    has_top_ = !heap_.empty();
    if (has_top_) {
        curr_idx_ = heap_.top();
        key_ = children_[curr_idx_].it->key();
    } else {
        curr_idx_ = static_cast<size_t>(-1);
        key_ = {};
    }
}

void QueryIterator::PushIfValid_(size_t idx) {
    auto& ch = children_[idx];
    if (!ch.it) return;
    if (!ch.it->Valid()) return;
    if (!InUpperBound(*icmp_, ch.it->key(), canon_upper_)) return;

    heap_.push(idx);
    ch.in_heap = true;
}

void QueryIterator::PopAndAdvanceTop_() {
    size_t i = heap_.top();
    heap_.pop();
    children_[i].in_heap = false;

    children_[i].it->Next();

    if (children_[i].it->Valid() &&
        InUpperBound(*icmp_, children_[i].it->key(), canon_upper_)) {
        heap_.push(i);
        children_[i].in_heap = true;
    }

    has_top_ = !heap_.empty();
    if (has_top_) {
        curr_idx_ = heap_.top();
        key_ = children_[curr_idx_].it->key();
    } else {
        curr_idx_ = static_cast<size_t>(-1);
        key_ = {};
    }
}

void QueryIterator::SetInternalRange(std::optional<std::string> lower,std::optional<std::string> upper) {
        assert(lower->size() == kIKeySize && upper->size() == kIKeySize);
        opts_.lower = lower;
        opts_.upper = upper;
        canon_lower_ = std::move(lower);
        canon_upper_ = std::move(upper);
}

Status QueryIterator::Init() {
    st_ = Status::OK();

    // 清空堆
    while (!heap_.empty()) heap_.pop();

    // 建立/收集 children
    Status s = BuildChildren_();
    if (!s.ok()) {
        st_ = s;
        has_top_ = false;
        curr_idx_ = static_cast<size_t>(-1);
        key_ = {};
        return st_;
    }

    // 初始化每個 child，定位至下界，並入堆
    for (size_t i = 0; i < children_.size(); ++i) {
        auto& ch = children_[i];
        if (!ch.it) continue;

        Status cs = ch.it->Init();
        if (!cs.ok()) {
            if (st_.ok()) st_ = cs; // 記錄第一個錯誤，但不中止
            continue;
        }

        if (canon_lower_) ch.it->Seek(*canon_lower_);
        else              ch.it->SeekToFirst();

        ch.in_heap = false;
        if (!ch.it->Valid()) continue;
        if (!InUpperBound(*icmp_, ch.it->key(), canon_upper_)) continue;

        heap_.push(i);
        ch.in_heap = true;
    }

    has_top_ = !heap_.empty();
    if (has_top_) {
        curr_idx_ = heap_.top();
        key_ = children_[curr_idx_].it->key();
    } else {
        curr_idx_ = static_cast<size_t>(-1);
        key_ = {};
    }

    return st_;
}

bool QueryIterator::Valid() const {
    return has_top_ && st_.ok();
}

void QueryIterator::SeekToFirst() {
    st_ = Status::OK();
    while (!heap_.empty()) heap_.pop();

    for (size_t i = 0; i < children_.size(); ++i) {
        auto& ch = children_[i];
        if (!ch.it) continue;

        ch.in_heap = false;
        if (canon_lower_) ch.it->Seek(*canon_lower_);
        else              ch.it->SeekToFirst();

        if (!ch.it->Valid()) continue;
        if (!InUpperBound(*icmp_, ch.it->key(), canon_upper_)) continue;

        heap_.push(i);
        ch.in_heap = true;
    }

    has_top_ = !heap_.empty();
    if (has_top_) { curr_idx_ = heap_.top(); key_ = children_[curr_idx_].it->key(); }
    else          { curr_idx_ = static_cast<size_t>(-1); key_ = {}; }
}


void QueryIterator::SeekToLast() {
    st_ = Status::NotSupported("QueryIterator::SeekToLast not supported yet");
}

void QueryIterator::Seek(std::string_view target) {
    st_ = Status::OK();
    if (canon_lower_ && HeapCmp::LessKey(*icmp_, target, *canon_lower_)) {
        SeekRebuildHeap_(*canon_lower_);
    } else {
        SeekRebuildHeap_(target);
    }
}

void QueryIterator::Next() {
    if (!has_top_) return;
    PopAndAdvanceTop_();
}

void QueryIterator::Prev() {
    st_ = Status::NotSupported("QueryIterator::Prev not supported");
}

std::string_view QueryIterator::key() const {
    return key_;
}


Status QueryIterator::ReadValue(std::string& out) const {
    if (!Valid()) {
        return Status::Corruption("QueryIterator not valid");
    }
    auto* it = children_[curr_idx_].it.get();
    if (!it) {
        return Status::Corruption("null child iterator");
    }
    if (!it->SupportsValueCopy()) {
        std::cout << "Current index " <<  curr_idx_ << std::endl;
        return Status::NotSupported("child iterator does not support value copy");
    }
    out.clear();
    return it->ReadValue(out);
}
