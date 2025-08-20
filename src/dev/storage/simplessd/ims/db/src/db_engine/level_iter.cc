#include "level_iter.hh"
#include <algorithm>

static constexpr size_t kIKeySize = sizeof(InternalKey);

// ---- ctor ----
Level0Iterator::Level0Iterator( SstableManager& smgr,
                                LOG_MANAGER&    lmgr,
                                const InternalKeyComparator* icmp,
                                LSMTree&        tree,
                                Options         opts)
    : smgr_(&smgr), lmgr_(&lmgr), icmp_(icmp), tree_(&tree), opts_(std::move(opts)),
      heap_(HeapCmp{icmp_, &children_}) {}

// ---- public ----
Status Level0Iterator::Init() {
    st_ = Status::OK();
    metas_.clear();
    children_.clear();
    clear_heap_();
    curr_idx_ = static_cast<size_t>(-1);
    build_bounds_from_opts_();
    if (!LoadL0Metas_()) {
        st_ = Status::IOError("LoadL0Metas failed");
        return st_;
    }
    children_.resize(metas_.size());
    for (size_t i = 0; i < metas_.size(); ++i) {
        children_[i].meta = metas_[i];
    }
    heap_ = Heap(HeapCmp{icmp_, &children_});

    // 建立 canonical internal bounds
    

    // 一次性開所有檔案
    if (auto s = open_all_children_(); !s.ok()) {
        st_ = s; return st_;
    }

    SeekToFirst();
    return st_;
}

bool Level0Iterator::Valid() const { return st_.ok() && has_top_; }

void Level0Iterator::SeekToFirst() {
    st_ = Status::OK();
    clear_heap_();
    curr_idx_ = static_cast<size_t>(-1);

    // 走所有 child，定位到 [lower, +∞) 的起點，且在 upper 之下
    for (size_t i = 0; i < children_.size(); ++i) {
        auto& ch = children_[i];
        if (!overlap_with_range_(ch.meta)) continue;

        if (canon_lower_) {
            std::string_view lo(canon_lower_->data(), canon_lower_->size());
            ch.it->Seek(lo);
        } else {
            ch.it->SeekToFirst();
        }
        if (ch.it->Valid() && within_upper_(ch.it->key())) push_heap_(i);
    }
    pull_top_();
}

void Level0Iterator::SeekToLast() {
    st_ = Status::OK();
    clear_heap_();
    curr_idx_ = static_cast<size_t>(-1);

    // 對每個 child，定位到 <= upper-ε 的最後一筆（若有 upper）
    std::string max_sentinel(kIKeySize, char(0xFF));
    for (size_t i = 0; i < children_.size(); ++i) {
        auto& ch = children_[i];
        if (!overlap_with_range_(ch.meta)) continue;

        if (canon_upper_) {
            std::string_view up(canon_upper_->data(), canon_upper_->size());
            ch.it->Seek(up);
            if (ch.it->Valid()) {
                // ch.key() >= up → 退一格
                if (!LessKey_((std::string_view)ch.it->key(), up)) ch.it->Prev();
            } else {
                ch.it->SeekToLast();
            }
        } else {
            // 沒 upper，用最大哨兵 seek，再 Prev 到最後有效
            ch.it->Seek(std::string_view(max_sentinel.data(), max_sentinel.size()));
            if (ch.it->Valid()) ch.it->Prev(); else ch.it->SeekToLast();
        }
        if (ch.it->Valid() && ge_lower_(ch.it->key())) push_heap_(i);
    }
    pull_top_max_();
}

void Level0Iterator::Seek(std::string_view internal_target) {
    st_ = Status::OK();
    clear_heap_();
    curr_idx_ = static_cast<size_t>(-1);

    assert(internal_target.size() == kIKeySize);
    for (size_t i = 0; i < children_.size(); ++i) {
        auto& ch = children_[i];
        if (!overlap_with_range_with_target_(ch.meta, internal_target)) continue;

        ch.it->Seek(internal_target);
        if (ch.it->Valid() && within_upper_(ch.it->key())) push_heap_(i);
    }
    pull_top_();
}

void Level0Iterator::Next() {
    if (!Valid()) return;
    size_t i = heap_.top(); heap_.pop();
    children_[i].in_heap = false;

    auto& it = *children_[i].it;
    it.Next();
    if (it.Valid() && within_upper_(it.key())) push_heap_(i);

    pull_top_();
}

void Level0Iterator::Prev() {
    if (!Valid()) { SeekToLast(); return; }

    // 反向遍歷：以當前 key 為上界，重建每個 child 的位置到 <= cur
    std::string cur(key_.data(), key_.size());
    clear_heap_();
    curr_idx_ = static_cast<size_t>(-1);

    for (size_t i = 0; i < children_.size(); ++i) {
        auto& ch = children_[i];
        if (!overlap_with_range_(ch.meta)) continue;

        ch.it->Seek(std::string_view(cur.data(), cur.size()));
        if (ch.it->Valid() && !LessKey_(ch.it->key(), cur)) ch.it->Prev();
        if (ch.it->Valid() && ge_lower_(ch.it->key())) push_heap_(i);
    }
    pull_top_max_();
}

std::string_view Level0Iterator::key() const { return has_top_ ? key_ : std::string_view{}; }
Status Level0Iterator::status() const { return st_; }

Status Level0Iterator::ReadValue(std::string& out) const {
    if (!Valid() || curr_idx_ == static_cast<size_t>(-1)) return Status::IOError("invalid iter");
    return children_[curr_idx_].it->ReadValue(out);
}

// ---- private helpers ----
Status Level0Iterator::open_all_children_() {
    for (size_t i = 0; i < children_.size(); ++i) {
        auto& ch = children_[i];
        if (ch.opened) continue;
        auto it = std::make_unique<SstableIterator>(*smgr_, *lmgr_, icmp_, ch.meta.filename,PACKING_T);
        auto s = it->Init();
        if (!s.ok()) return s;
        ch.it = std::move(it);
        ch.opened = true;
    }
    return Status::OK();
}

void Level0Iterator::clear_heap_() {
    while (!heap_.empty()) heap_.pop();
    for (auto& c : children_) c.in_heap = false;
    has_top_ = false; key_ = {};
}

void Level0Iterator::push_heap_(size_t i) {
    if (children_[i].in_heap) return;
    heap_.push(i);
    children_[i].in_heap = true;
}

void Level0Iterator::pull_top_() {
    if (heap_.empty()) { has_top_ = false; key_ = {}; curr_idx_ = static_cast<size_t>(-1); return; }
    curr_idx_ = heap_.top();
    key_ = children_[curr_idx_].it->key();
    has_top_ = true;
}

void Level0Iterator::pull_top_max_() {
    if (heap_.empty()) { has_top_ = false; key_ = {}; curr_idx_ = static_cast<size_t>(-1); return; }
    size_t best = heap_.top();
    std::vector<size_t> tmp;
    while (!heap_.empty()) { tmp.push_back(heap_.top()); heap_.pop(); }
    for (size_t idx : tmp) {
        InternalKey kb, bb;
        auto kv = children_[idx].it->key();
        auto bv = children_[best].it->key();
        assert(kv.size() == kIKeySize && bv.size() == kIKeySize);
        std::memcpy(&kb, kv.data(), kIKeySize);
        std::memcpy(&bb, bv.data(), kIKeySize);
        if (icmp_->operator()(bb, kb)) best = idx; // bb < kb → kb 更大
    }
    for (size_t idx : tmp) heap_.push(idx);
    curr_idx_ = best;
    key_ = children_[best].it->key();
    has_top_ = true;
}

bool Level0Iterator::HeapCmp::operator()(size_t a, size_t b) const {
    const auto& ca = *children->at(a).it;
    const auto& cb = *children->at(b).it;
    const auto ka = ca.key();
    const auto kb = cb.key();

    if (LessKey(*icmp, kb, ka)) return true;    // kb < ka → a 較大（放後面）
    if (LessKey(*icmp, ka, kb)) return false;   // ka < kb → a 較小（放前面）

    // tie-break：較小 file_id 視為較小
    auto fa = children->at(a).meta.file_id;
    auto fb = children->at(b).meta.file_id;
    return fa > fb;
}

bool Level0Iterator::HeapCmp::LessKey(const InternalKeyComparator& ic,
                                           std::string_view a, std::string_view b) {
    assert(a.size() == kIKeySize && b.size() == kIKeySize);
    InternalKey ia, ib;
    std::memcpy(&ia, a.data(), kIKeySize);
    std::memcpy(&ib, b.data(), kIKeySize);
    return ic(ia, ib);
}

bool Level0Iterator::LessKey_(std::string_view a, std::string_view b) const {
    return HeapCmp::LessKey(*icmp_, a, b);
}
bool Level0Iterator::LessKey_(const std::string& a, const std::string& b) const {
    return LessKey_(std::string_view(a.data(), a.size()),
                    std::string_view(b.data(), b.size()));
}

bool Level0Iterator::EqualKey_(std::string_view a, std::string_view b) const {
    assert(a.size() == kIKeySize && b.size() == kIKeySize);
    InternalKey ia, ib;
    std::memcpy(&ia, a.data(), kIKeySize);
    std::memcpy(&ib, b.data(), kIKeySize);
    return !icmp_->operator()(ia, ib) && !icmp_->operator()(ib, ia);
}

bool Level0Iterator::within_upper_(std::string_view k) const {
    if (!canon_upper_) return true;
    assert(canon_upper_->size() == kIKeySize);
    return LessKey_(k, std::string_view(canon_upper_->data(), canon_upper_->size())); // k < upper
}

bool Level0Iterator::ge_lower_(std::string_view k) const {
    if (!canon_lower_) return true;
    assert(k.size() == kIKeySize && canon_lower_->size() == kIKeySize);
    InternalKey a, b;
    std::memcpy(&a, k.data(), kIKeySize);
    std::memcpy(&b, canon_lower_->data(), kIKeySize);
    return !icmp_->operator()(a, b); // !(a < lower) → a >= lower
}

bool Level0Iterator::overlap_with_range_(const L0FileMeta& fm) const {
    if (canon_upper_) {
        InternalKey fmin, up;
        std::memcpy(&fmin, fm.min_key.data(), kIKeySize);
        std::memcpy(&up,   canon_upper_->data(), kIKeySize);
        if (!icmp_->operator()(fmin, up)) return false; // !(fmin < upper)
    }
    if (canon_lower_) {
        InternalKey fmax, lo;
        std::memcpy(&fmax, fm.max_key.data(), kIKeySize);
        std::memcpy(&lo,   canon_lower_->data(), kIKeySize);
        if (icmp_->operator()(fmax, lo)) return false; // fmax < lower
    }
    return true;
}

bool Level0Iterator::overlap_with_range_with_target_(const L0FileMeta& fm,
                                                          std::string_view target) const {
    assert(target.size() == kIKeySize);
    InternalKey fmax, t;
    std::memcpy(&fmax, fm.max_key.data(), kIKeySize);
    std::memcpy(&t,    target.data(),     kIKeySize);
    if (icmp_->operator()(fmax, t)) return false; // fmax < target
    if (canon_upper_) {
        InternalKey fmin, up;
        std::memcpy(&fmin, fm.min_key.data(), kIKeySize);
        std::memcpy(&up,   canon_upper_->data(), kIKeySize);
        if (!icmp_->operator()(fmin, up)) return false; // !(fmin < upper)
    }
    return true;
}

void Level0Iterator::build_bounds_from_opts_() {
    canon_lower_.reset();
    canon_upper_.reset();

    if (opts_.lower) {
        assert(opts_.lower->size() == kIKeySize);
        canon_lower_ = *opts_.lower;
    }
    if (opts_.upper) {
        assert(opts_.upper->size() == kIKeySize);
        canon_upper_ = *opts_.upper;
    }
}

// ---- Metadata loading (請接你們系統) ----
bool Level0Iterator::LoadL0Metas_() {
    if (!canon_lower_.has_value() || !canon_upper_.has_value()) {
        return false;
    }
    InternalKey lower{}, upper{};
    std::memcpy(&lower, canon_lower_->data(), kIKeySize);
    std::memcpy(&upper, canon_upper_->data(), kIKeySize);
    assert(canon_lower_->size() == kIKeySize && canon_upper_->size() == kIKeySize);
    auto sstables = tree_->search_one_level(0, lower.key, upper.key);
    for(auto &sstable : sstables) {
        L0FileMeta meta;
        meta.filename = sstable->filename;
        meta.min_key = InternalKey(sstable->rangeMin.toString(),0,ValueType::kTypeValue).Encode();
        meta.max_key = InternalKey(sstable->rangeMax.toString(),UINT64_MAX,ValueType::kTypeValue).Encode();
        meta.file_id = sstable->filename;
        metas_.push_back(meta);
    }
    return true;
}
