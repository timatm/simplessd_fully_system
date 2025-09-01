#include "compaction.hh"
#include <algorithm>
#include <limits>
#include <cstring>

// ===== 工具：安全解碼 / 萃取 user-key（translation unit 內可用） =====
static inline bool DecodeInternal(std::string_view s, InternalKey& out) {
    if (s.size() != sizeof(InternalKey)) {
        pr_debug("DecodeInternal: bad size=%zu (expect=%zu)", s.size(), sizeof(InternalKey));
        return false;
    }
    out = InternalKey::Decode(std::string(s.data(),s.size()));
    return true;
}

static inline bool ExtractUserKey(std::string_view ik, std::string& out) {
    InternalKey d{};
    if (!DecodeInternal(ik, d)) return false;
    out.assign(reinterpret_cast<const char*>(d.key.key), d.key.key_size);
    return true;
}

// ===== CompactionRunner =====

// CompactionRunner::CompactionRunner(SstableManager *smgr,
//                                    LogManager *lmgr,
//                                    LSMTree* tree,
//                                    const InternalKeyComparator* icmp,
//                                    PackingType type,
//                                    CompactionPlan srcC,
//                                    CompactionPlan dstC)
//     : smgr_(smgr),
//       lmgr_(lmgr),
//       tree_(tree),
//       icmp_(icmp),
//       nums_(0),
//       packType_(type),
//       srcConfig_(std::move(srcC)),
//       dstConfig_(std::move(dstC)) {
//     // 以 src 的 user 範圍推導 internal 極值邊界，並用來規範 src/dst 兩側 iterator 的觀察範圍
//     Options srcOption;
//     Options dstOption;

//     std::string src_u_lower, src_u_upper;
//     bool have_src_user_bounds = false;

//     // 正確處理 optional<string> → string_view
//     if (srcConfig_.lower_.has_value() && srcConfig_.upper_.has_value()) {
//         std::string_view lo_sv{ srcConfig_.lower_->data(), srcConfig_.lower_->size() };
//         std::string_view hi_sv{ srcConfig_.upper_->data(), srcConfig_.upper_->size() };
//         have_src_user_bounds =
//             ExtractUserKey(lo_sv, src_u_lower) &&
//             ExtractUserKey(hi_sv, src_u_upper);
//     }

//     if (have_src_user_bounds) {
//         InternalKey lo(src_u_lower, std::numeric_limits<uint64_t>::max(), ValueType::kTypeValue);
//         InternalKey hi(src_u_upper, 0,                             ValueType::kTypeValue);
//         srcOption.lower = lo.Encode();   // Options.lower/upper 若是 optional<string> 也可直接賦值
//         srcOption.upper = hi.Encode();
//         // 目的層只掃描與 src user 範圍重疊之檔案（用相同 internal 極值）
//         dstOption.lower = srcOption.lower;
//         dstOption.upper = srcOption.upper;
//     } else {
//         pr_debug("CompactionRunner: src bounds decode failed or not present, fallback to raw");
//         srcOption.lower = srcConfig_.lower_;  // optional<string> → optional<string>
//         srcOption.upper = srcConfig_.upper_;
//         dstOption.lower = dstConfig_.lower_;
//         dstOption.upper = dstConfig_.upper_;
//     }

//     if (srcConfig_.level_ == 0) {
//         srcLevelIter_ = std::make_unique<Level0Iterator>(smgr_, lmgr_, icmp_, tree_, srcOption, true);
//     } else if (srcConfig_.level_ > 0 && srcConfig_.level_ < MAX_LEVEL) {
//         srcLevelIter_ = std::make_unique<LevelNIterator>(smgr_, lmgr_, icmp_, tree_, srcConfig_.level_, srcOption);
//     } else {
//         pr_debug("CompactionRunner source level is error");
//     }

//     dstLevelIter_ = std::make_unique<LevelNIterator>(smgr_, lmgr_, icmp_, tree_, dstConfig_.level_, dstOption);

//     while (!sortedList_.empty()) sortedList_.pop();
//     hash_num_.clear();
//     if (packType_ == PackingType::kHash) hash_num_.assign(SLOT_NUM, 0);
// }




CompactionRunner::CompactionRunner( SstableManager *smgr,
                                    LogManager *lmgr,
                                    LSMTree *tree,
                                    const InternalKeyComparator* icmp,
                                    PackingType type,
                                    int level,
                                    std::vector<std::shared_ptr<TreeNode>> srcSstables,
                                    std::vector<std::shared_ptr<TreeNode>> dstSstables)
        :   smgr_(smgr),
            lmgr_(lmgr),
            tree_(tree),
            icmp_(icmp),
            nums_(0),
            packType_(type),
            srcLevel_(level){

            if (level == 0) {
                srcLevelIter_ = std::make_unique<Level0Iterator>(smgr_, lmgr_, icmp_, tree_, std::move(srcSstables), true);
            } else if (level > 0 && level < MAX_LEVEL) {
                srcLevelIter_ = std::make_unique<LevelNIterator>(smgr_, lmgr_, icmp_, tree_, level, std::move(srcSstables),true);
            } else {
                pr_debug("CompactionRunner source level is error");
            }

            dstLevelIter_ = std::make_unique<LevelNIterator>(smgr_, lmgr_, icmp_, tree_, (level+1) , std::move(dstSstables),true);

            while (!sortedList_.empty()) sortedList_.pop();
            hash_num_.clear();
            if (packType_ == PackingType::kHash) hash_num_.assign(SLOT_NUM_PER_PAGE, 0);
        }

bool CompactionRunner::same_user_key(std::string_view a, std::string_view b) {
    InternalKey ia{}, ib{};
    if (!DecodeInternal(a, ia) || !DecodeInternal(b, ib)) return false;
    if (ia.key.key_size != ib.key.key_size) return false;
    return std::memcmp(ia.key.key, ib.key.key, ia.key.key_size) == 0;
}

uint8_t CompactionRunner::value_type_of(std::string_view ikey) {
    InternalKey ik{};
    if (!DecodeInternal(ikey, ik)) return (uint8_t)ValueType::kTypeValue;
    return static_cast<uint8_t>(ik.info.type);
}

bool CompactionRunner::memTableIsFull() {
    switch (packType_) {
        case PackingType::kKeyPerPage:
            return nums_ >= IMS_PAGE_NUM;
        case PackingType::kHash:
            return std::any_of(hash_num_.begin(), hash_num_.end(),
                               [](uint32_t count) { return count >= IMS_PAGE_NUM; });
        case PackingType::kKeyRange:
            return nums_ >= SLOT_NUM_PER_PAGE * IMS_PAGE_NUM;
        default:
            return false;
    }
}


Status CompactionRunner::Run() {
    if (!srcLevelIter_ || !dstLevelIter_) return Status::IOError("iterators not ready");
    auto s = srcLevelIter_->Init();
    if (!s.ok())
    {
        pr_debug("Source level iterator initialization is failed");
        return s;
    } 
    auto t = dstLevelIter_->Init();
    if (!t.ok()){
        pr_debug("Destination level iterator initialization is failed");
        return t;
    } 

    auto equal_internal = [&](const InternalKey& a, const InternalKey& b)->bool {
        return std::memcmp(&a, &b, sizeof(InternalKey)) == 0;
    };

    // flush()：把目前 chunk 寫成一個檔，並重置所有累計
    auto flush = [&]() -> Status {
        if (sortedList_.empty()) return Status::OK();

        // 基本防呆
        if (sortedList_.front().size() != sizeof(InternalKey) ||
            sortedList_.back().size()  != sizeof(InternalKey)) {
            pr_debug("flush: bad internal key size (front/back)");
            return Status::IOError("flush: bad internal key size");
        }
        InternalKey minK = InternalKey::Decode(sortedList_.front());
        InternalKey maxK = InternalKey::Decode(sortedList_.back());
        
        auto buffer = smgr_->packingTable(sortedList_);
        // pr_debug("Compaction write SStable info");
        // minK.dump();
        // maxK.dump();
        smgr_->writeSSTable( static_cast<uint8_t>(srcLevel_ + 1), minK, maxK, std::move(buffer), /*clearImmuteTable=*/false);
        // 清空 queue、重置計數
        

        while (!sortedList_.empty()) sortedList_.pop();
        nums_ = 0;
        if (packType_ == PackingType::kHash) std::fill(hash_num_.begin(), hash_num_.end(), 0);
        return Status::OK();
    };

    // emit()：把一個 key 放入 chunk；若滿了就 flush()
    auto emit = [&](std::string_view k) -> Status {
        // 實際拷貝一份（queue 內持有）
        InternalKey a = InternalKey::Decode(std::string(k.data(),k.size()));
        if(a.key.key_size == 0){
            pr_debug("Compation sorted list insert a error internal key");
            a.dump();
            return Status::OK();
        }
        sortedList_.emplace(k.data(), k.size());

        if (packType_ == PackingType::kHash) {
            InternalKey key{};
            if (!DecodeInternal(k, key)) return Status::IOError("emit: bad internal key (hash)");
            auto idx = HashModN(key, SLOT_NUM_PER_PAGE);
            if (idx >= hash_num_.size()) return Status::IOError("hash_num_ not initialized");
            hash_num_[idx]++;
        } else {
            ++nums_;
        }

        if (memTableIsFull()) {
            return flush();
        }
        return Status::OK();
    };

    // 兩路併合
    bool l_valid = srcLevelIter_->Valid();
    bool r_valid = dstLevelIter_->Valid();

    std::string last_user_key; // 折疊同 user（internal 同 user 按 seq 降序）
    bool have_last = false;

    // 單調性檢查（debug）
    bool has_prev = false;
    InternalKey prev_internal{};

    while (l_valid || r_valid) {
        bool take_left = false;
        if (!r_valid) {
            take_left = true;
        } else if (!l_valid) {
            take_left = false;
        } else {
            InternalKey lk{}, rk{};
            bool ld = DecodeInternal(srcLevelIter_->key(), lk);
            bool rd = DecodeInternal(dstLevelIter_->key(), rk);
            if (!rd && !ld) {
                pr_debug("both keys decode failed; default take_left");
                take_left = true;
            } else if (!rd) {
                take_left = true;
            } else if (!ld) {
                take_left = false;
            } else {
                take_left = (*icmp_)(lk, rk); // 正常比較（internal 升序）
            }
        }

        std::string cur_owned;
        if (take_left) {
            std::string_view sv = srcLevelIter_->key();
            cur_owned.assign(sv.data(), sv.size());
            srcLevelIter_->Next();
            l_valid = srcLevelIter_->Valid();
        } else {
            std::string_view sv = dstLevelIter_->key();
            cur_owned.assign(sv.data(), sv.size());
            dstLevelIter_->Next();
            r_valid = dstLevelIter_->Valid();
        }
        std::string_view cur_key(cur_owned.data(), cur_owned.size());

        
        InternalKey cur_internal{};
        if (DecodeInternal(cur_key, cur_internal)) {
            if (has_prev && !(*icmp_)(prev_internal, cur_internal) && !equal_internal(prev_internal, cur_internal)) {
                pr_debug("Compaction non-monotonic: previous > current (internal order broken)");
            }
            if(cur_internal.key.key_size == 0){
                pr_debug("Internal key is error");
                continue;
            }
            prev_internal = cur_internal;
            has_prev = true;
        }

        // 版本折疊（同 user 只留第一個 = 最新）
        std::string cur_user;
        if (!ExtractUserKey(cur_key, cur_user)) {
            pr_debug("extract_user_key failed, skip");
            continue;
        }
        if (!have_last || cur_user != last_user_key) {
            last_user_key = std::move(cur_user);
            have_last = true;
            auto es = emit(cur_key);
            if (!es.ok()){
                pr_debug("Comaption error 0");
                return es;
            } 
            else {
                continue; // 同 user 舊版本丟棄
            }
        }
    }

    // 收尾 flush
    if (!sortedList_.empty()) {
        auto fs = flush();
        if (!fs.ok()){
            pr_debug("Comaption error 1");
            return fs;
        }
    }

    smgr_->waitAllTasksDone();
    return Status::OK();
}
