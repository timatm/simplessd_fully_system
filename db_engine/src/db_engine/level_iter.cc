#include "level_iter.hh"
#include <algorithm>
#include <limits>      // std::numeric_limits
#include <stdexcept>   // std::invalid_argument
#include <cstdint>     // uint64_t（若未在别处包含）

static constexpr size_t kIKeySize = sizeof(InternalKey);



inline InternalKey LowerSentinel(const std::string& uk) {
    return InternalKey(uk, UINT64_MAX, ValueType::kTypeMin);
};
inline InternalKey UpperSentinel(const std::string& uk) {
    return InternalKey(uk, 0,          ValueType::kTypeMax);
};

uint64_t to_u64_stoull(const std::string& s, int base = 10) {
    size_t pos = 0;
    uint64_t v = std::stoull(s, &pos, base); // base=10；若想自动识别 0x 前缀，用 base=0
    if (pos != s.size()) throw std::invalid_argument("trailing chars");
    return v;
}

// ---- ctor ----
Level0Iterator::Level0Iterator( SstableManager* smgr,
                                LogManager*    lmgr,
                                const InternalKeyComparator* icmp,
                                LSMTree*        tree,
                                Options         opts,
                                bool            IsCompaction)
    : smgr_(smgr), lmgr_(lmgr), icmp_(icmp), tree_(tree), opts_(std::move(opts)),
      heap_(HeapCmp{icmp_, &children_}),compaction_(IsCompaction) {
        metas_.clear();
      }


Level0Iterator::Level0Iterator( SstableManager* smgr,
                                LogManager*    lmgr,
                                const InternalKeyComparator* icmp,
                                LSMTree*        tree,
                                std::vector<std::shared_ptr<TreeNode>> sstables,
                                bool            IsCompaction): 
    smgr_(smgr), lmgr_(lmgr), icmp_(icmp), tree_(tree),
    heap_(HeapCmp{icmp_, &children_}),compaction_(IsCompaction) {
        metas_.clear();
        uint64_t fid_fallback = 0;
        for (auto& sstable : sstables) {
            if (!sstable) continue;
            L0FileMeta meta;
            meta.filename = sstable->filename;
            meta.min_key  = LowerSentinel(sstable->rangeMin.toString()).Encode();
            meta.max_key  = UpperSentinel(sstable->rangeMax.toString()).Encode();
            if (meta.min_key.size() != kIKeySize || meta.max_key.size() != kIKeySize){
                pr_debug("Internal key size is error");
            }
            // 檔名非純數字時避免丟例外
            try {
                meta.file_id = to_u64_stoull(sstable->filename);
            } catch (...) {
                pr_debug("Sstable filename is error");
                meta.file_id = fid_fallback++; // 保底 tie-break
            }
            metas_.push_back(std::move(meta));
        }
    }



// ---- public ----
Status Level0Iterator::Init() {
    st_ = Status::OK();
    children_.clear();
    clear_heap_();
    curr_idx_ = static_cast<size_t>(-1);
    build_bounds_from_opts_();
    if(metas_.empty()){
        if (!LoadL0Metas_()) {
            st_ = Status::IOError("LoadL0Metas failed");
            return st_;
        }
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
            if(lo.size() != sizeof(InternalKey)){
                pr_debug("The internal key size is invalid");
                std::cout << "KEY: " << lo.data() << " size: " << lo.size() << std::endl; 
            }
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

    std::string_view tgt = internal_target;
    if (canon_lower_) {
        std::string_view lo(canon_lower_->data(), canon_lower_->size());
        if (LessKey_(tgt, lo)) tgt = lo;
    }

    assert(internal_target.size() == kIKeySize);
    for (size_t i = 0; i < children_.size(); ++i) {
        auto& ch = children_[i];
        if (!overlap_with_range_with_target_(ch.meta, internal_target)) continue;

        ch.it->Seek(tgt);
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
        if (!ch.it->Valid()) {
            // cur 比此文件的所有 key 都大 → 回到该文件的最后一个 key
            ch.it->SeekToLast();
        } else if (!LessKey_(ch.it->key(), cur)) {
            // 否则，如果 key >= cur，则退一格，落到 < cur
            ch.it->Prev();
        }
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
        auto it = std::make_unique<SstableIterator>(smgr_, lmgr_, icmp_, ch.meta.filename,PACKING_T);
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
    if (heap_.empty()) { has_top_ = false; key_ = {}; curr_idx_ = (size_t)-1; return; }

    size_t last = (size_t)-1;
    std::vector<size_t> tmp;
    tmp.reserve(heap_.size());

    while (!heap_.empty()) {
        last = heap_.top();           // 记录“当前弹出的”
        tmp.push_back(last);
        heap_.pop();
    }

    for (size_t idx : tmp) heap_.push(idx);  // 还原堆

    curr_idx_ = last;                        // last 就是“最大值”
    key_      = children_[last].it->key();
    has_top_  = true;
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



bool Level0Iterator::LoadL0Metas_() {
    metas_.clear();

    // 1) 決定查詢的 user-key 區間
    Key qmin;                              // 最小：空 key
    Key qmax; qmax.key_size = 40; std::memset(qmax.key, 0xFF, 40); // 最大：40B 0xFF

    if (canon_lower_.has_value()) {
        if (canon_lower_->size() != kIKeySize) return false;
        InternalKey lower{};
        lower = InternalKey::Decode(canon_lower_->data());
        qmin = lower.key;
    }
    if (canon_upper_.has_value()) {
        if (canon_upper_->size() != kIKeySize) return false;
        InternalKey upper{};
        upper = InternalKey::Decode(canon_upper_->data());
        qmax = upper.key;
    }

    // 2) 取覆蓋此 user 區間的 L0 檔案（vector）
    auto sstables = tree_->search_one_level(0, qmin, qmax);

    metas_.reserve(sstables.size());
    uint64_t fid_fallback = 0;
    for (auto& sstable : sstables) {
        if (!sstable) continue;
        L0FileMeta meta;
        meta.filename = sstable->filename;
        meta.min_key  = LowerSentinel(sstable->rangeMin.toString()).Encode();
        meta.max_key  = UpperSentinel(sstable->rangeMax.toString()).Encode();
        if (meta.min_key.size() != kIKeySize || meta.max_key.size() != kIKeySize) return false;

        // 檔名非純數字時避免丟例外
        try {
            meta.file_id = to_u64_stoull(sstable->filename);
        } catch (...) {
            meta.file_id = fid_fallback++; // 保底 tie-break
        }
        metas_.push_back(std::move(meta));
    }

    // 4) compaction 模式：載完 metas 才清掉上下界，後續 Seek/SeekToFirst 無界遍歷
    if (compaction_) {
        canon_lower_.reset();
        canon_upper_.reset();
    }
    return true;
}



LevelNIterator::LevelNIterator( SstableManager* smgr,
                                LogManager*    lmgr,
                                const InternalKeyComparator* icmp,
                                LSMTree*        tree,
                                int             level,
                                Options         opts)
    : smgr_(smgr), lmgr_(lmgr), icmp_(icmp), tree_(tree), level_(level), opts_(std::move(opts)) {
        metas_.clear();
    }


LevelNIterator::LevelNIterator( SstableManager* smgr,
                                LogManager*     lmgr,
                                const InternalKeyComparator* icmp,
                                LSMTree*        tree,
                                int             level,
                                std::vector<std::shared_ptr<TreeNode>> sstables,
                                bool            compaction)
    : smgr_(smgr), lmgr_(lmgr), icmp_(icmp), tree_(tree), level_(level),compaction_(compaction) {
        metas_.clear();
        for (auto sstable : sstables) {
            L0FileMeta meta;
            meta.filename = sstable->filename;

            meta.min_key  = LowerSentinel(sstable->rangeMin.toString()).Encode();
            meta.max_key  = UpperSentinel(sstable->rangeMax.toString()).Encode();
            if (meta.min_key.size() != kIKeySize || meta.max_key.size() != kIKeySize){
                pr_debug("Internal key size is error");
            }
            meta.file_id = std::stoull(sstable->filename);
            metas_.push_back(std::move(meta));
        }
    }


Status LevelNIterator::Init() {
    st_ = Status::OK();
    children_.clear();
    open_lru_.clear();
    reset_view_();

    build_bounds_from_opts_();
    if(metas_.empty()){
        if (!LoadLevelMetas_()) {
            st_ = Status::IOError("LoadLevelMetas failed");
            return st_;
        }
    }
    children_.resize(metas_.size());
    for (size_t i = 0; i < metas_.size(); ++i) children_[i].meta = metas_[i];

    // 不開檔，在第一次使用時才 lazy open
    SeekToFirst();
    return st_;
}






bool LevelNIterator::Valid() const { return st_.ok() && has_top_; }

void LevelNIterator::SeekToFirst() {
    st_ = Status::OK();
    reset_view_();

    if (children_.empty()) return;

    size_t i = find_first_file_ge_lower_();
    for (; i < children_.size(); ++i) {
        if (position_child_to_first_(i)) {
            cur_file_ = i;
            key_ = children_[i].it->key();
            has_top_ = true;
            return;
        }
    }
    has_top_ = false; key_ = {};
    cur_file_ = static_cast<size_t>(-1);
}

void LevelNIterator::SeekToLast() {
    st_ = Status::OK();
    reset_view_();

    if (children_.empty()) return;

    size_t i = find_last_file_lt_upper_();
    if (i == static_cast<size_t>(-1) || i >= children_.size()) { has_top_ = false; return; }

    for (;;){
        if (position_child_to_last_(i)) {
            cur_file_ = i;
            key_ = children_[i].it->key();
            has_top_ = true;
            return;
        }
        if (i == 0) break;
        --i;
    }
    has_top_ = false;
    key_ = {};
    cur_file_ = static_cast<size_t>(-1);
}

void LevelNIterator::Seek(std::string_view internal_target) {
    st_ = Status::OK();
    reset_view_();
    if (children_.empty()) return;

    assert(internal_target.size() == kIKeySize);

    // clamp 到 lower
    std::string_view tgt = internal_target;
    if (canon_lower_) {
        if (LessKey_(tgt, *canon_lower_)) tgt = *canon_lower_;
    }

    size_t i = find_file_for_target_(tgt);
    if (i == children_.size()) {
        i = find_first_file_ge_lower_();
    }

    for (; i < children_.size(); ++i) {
        if (position_child_to_target_ge_(i, tgt)) {
            // 再檢查一下下界
            if (!ge_lower_(children_[i].it->key())) continue;
            cur_file_ = i;
            key_ = children_[i].it->key(); 
            has_top_ = true;
            return;
        }
    }
    has_top_ = false;
    key_ = {};
    cur_file_ = static_cast<size_t>(-1);
}

void LevelNIterator::Next() {
    if (!Valid()) return;
    auto& it = *children_[cur_file_].it;
    it.Next();
    if (it.Valid() && within_upper_(it.key())) {
        key_ = it.key();
        return;
    }
    // 換到下一檔
    size_t i = cur_file_ + 1;
    for (; i < children_.size(); ++i) {
        if (position_child_to_first_(i)) {
            cur_file_ = i;
            key_ = children_[i].it->key();
            return;
        }
    }
    has_top_ = false;
    key_ = {};
    cur_file_ = static_cast<size_t>(-1);
}

void LevelNIterator::Prev() {
    if (!Valid()) { SeekToLast(); return; }

    auto& it = *children_[cur_file_].it;
    it.Prev();
    if (it.Valid() && ge_lower_(it.key())) { key_ = it.key(); return; }

    // 需換到前一檔
    if (cur_file_ == 0) {
        has_top_ = false;
        key_ = {};
        cur_file_ = static_cast<size_t>(-1);
        return;
    }
    size_t i = cur_file_;
    do {
        --i;
        if (position_child_to_last_(i)){
            cur_file_ = i;
            key_ = children_[i].it->key();
            return;
        }
    } while (i > 0);

    has_top_ = false; key_ = {}; cur_file_ = static_cast<size_t>(-1);
}

std::string_view LevelNIterator::key() const { return has_top_ ? key_ : std::string_view{}; }
Status LevelNIterator::ReadValue(std::string& out) const {
    if (!Valid() || cur_file_ == static_cast<size_t>(-1)) return Status::IOError("invalid iter");
    return children_[cur_file_].it->ReadValue(out);
}
Status LevelNIterator::status() const { return st_; }

// ----------------- helpers -----------------
void LevelNIterator::reset_view_() {
    has_top_ = false;
    key_ = {};
    cur_file_ = static_cast<size_t>(-1);
}

bool LevelNIterator::within_upper_(std::string_view k) const {
    if (!canon_upper_) return true;
    assert(canon_upper_->size() == kIKeySize);
    return LessKey_(k, std::string_view(canon_upper_->data(), canon_upper_->size())); // k < upper
}
bool LevelNIterator::ge_lower_(std::string_view k) const {
    if (!canon_lower_) return true;
    assert(k.size() == kIKeySize && canon_lower_->size() == kIKeySize);
    InternalKey a{}, b{};
    std::memcpy(&a, k.data(), kIKeySize);
    std::memcpy(&b, canon_lower_->data(), kIKeySize);
    return !(*icmp_)(a, b); // !(a < lower)
}

bool LevelNIterator::LessKey_(std::string_view a, std::string_view b) const {
    assert(a.size() == kIKeySize && b.size() == kIKeySize);
    InternalKey ia{}, ib{};
    std::memcpy(&ia, a.data(), kIKeySize);
    std::memcpy(&ib, b.data(), kIKeySize);
    return (*icmp_)(ia, ib);
}
bool LevelNIterator::LessKey_(const std::string& a, const std::string& b) const {
    return LessKey_(std::string_view(a.data(), a.size()),
                    std::string_view(b.data(), b.size()));
}

void LevelNIterator::build_bounds_from_opts_() {
    canon_lower_.reset();
    canon_upper_.reset();
    if (opts_.lower) { assert(opts_.lower->size() == kIKeySize); canon_lower_ = *opts_.lower; }
    if (opts_.upper) { assert(opts_.upper->size() == kIKeySize); canon_upper_ = *opts_.upper; }
}

bool LevelNIterator::LoadLevelMetas_() {
    if(compaction_) return true;
    metas_.clear();
    if (!canon_lower_.has_value() || !canon_upper_.has_value()) return false;
    if (canon_lower_->size() != kIKeySize || canon_upper_->size() != kIKeySize) return false;

    InternalKey lower{}, upper{};
    std::memcpy(&lower, canon_lower_->data(), kIKeySize);
    std::memcpy(&upper, canon_upper_->data(), kIKeySize);

    auto sstables = tree_->search_one_level(level_, lower.key, upper.key);

    using VTU = std::underlying_type_t<ValueType>;
    const auto kMinType = static_cast<ValueType>(std::numeric_limits<VTU>::min());
    const auto kMaxType = static_cast<ValueType>(std::numeric_limits<VTU>::max());

    for (auto sstable : sstables) {
        L0FileMeta meta;
        meta.filename = sstable->filename;
        meta.min_key = InternalKey(sstable->rangeMin.toString(), 0ull,  kMinType).Encode();
        meta.max_key = InternalKey(sstable->rangeMax.toString(), std::numeric_limits<uint64_t>::max(), kMaxType).Encode();
        if (meta.min_key.size() != kIKeySize || meta.max_key.size() != kIKeySize) return false;
        meta.file_id = std::stoull(sstable->filename);
        metas_.push_back(std::move(meta));
    }

    std::sort(metas_.begin(), metas_.end(), [&](const L0FileMeta& a, const L0FileMeta& b){
        return LessKey_(a.min_key, b.min_key);
    });
    return true;
}

// ---- 定位與切檔（lazy open 版本） ----
bool LevelNIterator::position_child_to_first_(size_t i) {
    if (i >= children_.size()) return false;
    if (auto s = ensure_child_open_(i); !s.ok()) { st_ = s; return false; }

    auto& it = *children_[i].it;
    if (canon_lower_) it.Seek(std::string_view(canon_lower_->data(), canon_lower_->size()));
    else              it.SeekToFirst();

    if (!it.Valid()) return false;
    if (!within_upper_(it.key())) return false;
    return true;
}

bool LevelNIterator::position_child_to_last_(size_t i) {
    if (i >= children_.size()) return false;
    if (auto s = ensure_child_open_(i); !s.ok()) { st_ = s; return false; }

    auto& it = *children_[i].it;
    if (canon_upper_) {
        std::string_view up(canon_upper_->data(), canon_upper_->size());
        it.Seek(up);
        if (it.Valid()) {
            if (!LessKey_(it.key(), up)) it.Prev();  // 若 >= upper 就退一格
        } else {
            it.SeekToLast();
        }
    } else {
        it.SeekToLast();
    }
    if (!it.Valid()) return false;
    if (!ge_lower_(it.key())) return false;
    return true;
}

bool LevelNIterator::position_child_to_target_ge_(size_t i, std::string_view tgt) {
    if (i >= children_.size()) return false;
    if (auto s = ensure_child_open_(i); !s.ok()) { st_ = s; return false; }

    auto& it = *children_[i].it;
    it.Seek(tgt);
    if (!it.Valid()) return false;
    if (!within_upper_(it.key())) return false;
    return true;
}

size_t LevelNIterator::find_first_file_ge_lower_() const {
    if (!canon_lower_) return 0;
    size_t lo = 0, hi = metas_.size();
    while (lo < hi) {
        size_t md = (lo + hi) >> 1;
        if (LessKey_(metas_[md].max_key, *canon_lower_)) lo = md + 1; // max < lower → 往右
        else hi = md;
    }
    return lo;
}

size_t LevelNIterator::find_last_file_lt_upper_() const {
    if (metas_.empty()) return static_cast<size_t>(-1);
    if (!canon_upper_) return metas_.size() - 1;

    size_t lo = 0, hi = metas_.size();
    while (lo < hi) {
        size_t md = (lo + hi) >> 1;
        if (!LessKey_(metas_[md].min_key, *canon_upper_)) hi = md; // !(min < upper) → min ≥ upper
        else lo = md + 1;
    }
    if (lo == 0) return static_cast<size_t>(-1);
    return lo - 1;
}

size_t LevelNIterator::find_file_for_target_(std::string_view target) const {
    size_t lo = 0, hi = metas_.size();
    while (lo < hi) {
        size_t md = (lo + hi) >> 1;
        if (LessKey_(metas_[md].max_key, target)) lo = md + 1;        // max < target → 右邊
        else if (LessKey_(target, metas_[md].min_key)) hi = md;        // target < min → 左邊
        else return md;                                                // 落在此檔
    }
    return metas_.size();
}

// ---- Lazy open + LRU ----
Status LevelNIterator::ensure_child_open_(size_t i) {
    auto& ch = children_[i];
    if (ch.opened) {
        lru_touch_(i);
        return Status::OK();
    }

    // 先開，後驅逐（避免把自己立刻趕走）
    auto it = std::make_unique<SstableIterator>(smgr_, lmgr_, icmp_, ch.meta.filename, PACKING_T);
    auto s = it->Init();
    if (!s.ok()) return s;

    ch.it = std::move(it);
    ch.opened = true;
    lru_touch_(i);
    lru_evict_if_needed_();
    return Status::OK();
}

void LevelNIterator::lru_touch_(size_t i) {
    // 移除舊位置
    for (auto it = open_lru_.begin(); it != open_lru_.end(); ++it) {
        if (*it == i) {
            open_lru_.erase(it); 
            break;
        }
    }
    open_lru_.push_back(i);
}

void LevelNIterator::lru_evict_if_needed_() {
    while (open_lru_.size() > max_open_children_) {
        size_t victim = open_lru_.front();
        open_lru_.pop_front();
        if (victim == cur_file_) {
            // 避免把正在使用的游標關掉，放到隊尾重新考慮
            open_lru_.push_back(victim);
            if (open_lru_.size() == 1) return; // 只有自己，無法驅逐
            continue;
        }
        auto& ch = children_[victim];
        ch.it.reset();
        ch.opened = false;
    }
}
