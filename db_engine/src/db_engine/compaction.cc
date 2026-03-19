#include "compaction.hh"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>

thread_local bool     g_comp_trace_on       = false;
thread_local uint64_t g_comp_materialize_ns = 0;
thread_local uint64_t g_comp_write_ns       = 0;

std::atomic<uint64_t> g_cmp_sim_nand_ns{0};
std::atomic<uint64_t> g_cmp_sim_nand_calls{0};

std::atomic<uint64_t> g_cmp_run_count{0};
std::atomic<uint64_t> g_cmp_run_total_ns{0};
std::atomic<uint64_t> g_cmp_read_sstable_ns{0};
std::atomic<uint64_t> g_cmp_merge_core_ns{0};
std::atomic<uint64_t> g_cmp_pack_ns{0};
std::atomic<uint64_t> g_cmp_write_submit_ns{0};
std::atomic<uint64_t> g_cmp_write_stage_ns{0};
std::atomic<uint64_t> g_cmp_wait_ns{0};

namespace {
using Clock = std::chrono::steady_clock;
using Ns    = std::chrono::nanoseconds;

static inline uint64_t ToNs(Clock::duration d) {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<Ns>(d).count());
}
}

static inline bool DecodeInternal(std::string_view s, InternalKey& out) {
    if (s.size() != sizeof(InternalKey)) {
        pr_error("DecodeInternal: bad size=%zu (expect=%zu)", s.size(), sizeof(InternalKey));
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
                                    std::vector<std::shared_ptr<TreeNode>> dstSstables,
                                    uint32_t &sstable_write_count)
        :   smgr_(smgr),
            lmgr_(lmgr),
            tree_(tree),
            icmp_(icmp),
            nums_(0),
            packType_(type),
            srcLevel_(level),
            sstable_write_count_compaction(sstable_write_count){

            if (level == 0) {
                srcLevelIter_ = std::make_unique<Level0Iterator>(smgr_, lmgr_, icmp_, tree_, std::move(srcSstables), true);
            } else if (level > 0 && level < MAX_LEVEL) {
                srcLevelIter_ = std::make_unique<LevelNIterator>(smgr_, lmgr_, icmp_, tree_, level, std::move(srcSstables),true);
            } else {
                pr_error("CompactionRunner source level is error");
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
        case PackingType::kIdxBloomData: {
            const uint32_t meta_pages = static_cast<uint32_t>(IDX_BLOOM_META_PAGES);
            const uint32_t slots_per_page = IMS_PAGE_SIZE / sizeof(InternalKey);
            const uint32_t data_pages_cap = IMS_PAGE_NUM - meta_pages;
            const uint32_t max_entries = data_pages_cap * slots_per_page;
            return nums_ >= max_entries;
        }
        default:
            return false;
    }
}


Status CompactionRunner::Run() {
    if (!srcLevelIter_ || !dstLevelIter_) {
        return Status::IOError("iterators not ready");
    }

    const auto run_begin = Clock::now();

    g_comp_trace_on       = true;
    g_comp_materialize_ns = 0;
    g_comp_write_ns       = 0;

    uint64_t pack_ns = 0;
    uint64_t wait_ns = 0;
    uint64_t flush_cnt = 0;
    uint64_t emitted_keys = 0;
    uint64_t dropped_old_versions = 0;

    auto finish = [&](Status st) -> Status {
        const uint64_t total_ns       = ToNs(Clock::now() - run_begin);
        const uint64_t materialize_ns = g_comp_materialize_ns;
        const uint64_t write_submit_ns = g_comp_write_ns;

        uint64_t merge_core_ns = 0;
        if (total_ns > materialize_ns + pack_ns + write_submit_ns + wait_ns) {
            merge_core_ns = total_ns - materialize_ns - pack_ns - write_submit_ns - wait_ns;
        }

        g_cmp_run_count.fetch_add(1, std::memory_order_relaxed);
        g_cmp_run_total_ns.fetch_add(total_ns, std::memory_order_relaxed);
        g_cmp_read_sstable_ns.fetch_add(materialize_ns, std::memory_order_relaxed);
        g_cmp_merge_core_ns.fetch_add(merge_core_ns, std::memory_order_relaxed);
        g_cmp_pack_ns.fetch_add(pack_ns, std::memory_order_relaxed);
        g_cmp_write_submit_ns.fetch_add(write_submit_ns, std::memory_order_relaxed);
        g_cmp_write_stage_ns.fetch_add(pack_ns + write_submit_ns, std::memory_order_relaxed);
        g_cmp_wait_ns.fetch_add(wait_ns, std::memory_order_relaxed);

        pr_stat("[CMP-RUN] L%d->L%d total_ms=%.3f read_sstable_ms=%.3f "
                "merge_core_ms=%.3f pack_ms=%.3f write_submit_ms=%.3f "
                "write_stage_ms=%.3f wait_ms=%.3f flush_cnt=%llu emit_keys=%llu drop_old=%llu",
                srcLevel_, srcLevel_ + 1,
                static_cast<double>(total_ns) / 1e6,
                static_cast<double>(materialize_ns) / 1e6,
                static_cast<double>(merge_core_ns) / 1e6,
                static_cast<double>(pack_ns) / 1e6,
                static_cast<double>(write_submit_ns) / 1e6,
                static_cast<double>(pack_ns + write_submit_ns) / 1e6,
                static_cast<double>(wait_ns) / 1e6,
                (unsigned long long)flush_cnt,
                (unsigned long long)emitted_keys,
                (unsigned long long)dropped_old_versions);

        g_comp_trace_on       = false;
        g_comp_materialize_ns = 0;
        g_comp_write_ns       = 0;
        return st;
    };

    auto s = srcLevelIter_->Init();
    if (!s.ok()) {
        pr_error("Source level iterator initialization is failed");
        return finish(s);
    }

    auto t = dstLevelIter_->Init();
    if (!t.ok()) {
        pr_error("Destination level iterator initialization is failed");
        return finish(t);
    }

    auto equal_internal = [&](const InternalKey& a, const InternalKey& b)->bool {
        return std::memcmp(&a, &b, sizeof(InternalKey)) == 0;
    };

    auto flush = [&]() -> Status {
        if (sortedList_.empty()) return Status::OK();

        flush_cnt++;
        sstable_write_count_compaction++;

        if (sortedList_.front().size() != sizeof(InternalKey) ||
            sortedList_.back().size()  != sizeof(InternalKey)) {
            pr_error("flush: bad internal key size (front/back)");
            return Status::IOError("flush: bad internal key size");
        }

        InternalKey minK = InternalKey::Decode(sortedList_.front());
        InternalKey maxK = InternalKey::Decode(sortedList_.back());

        const auto pack_begin = Clock::now();
        auto buffer = smgr_->packingTable(sortedList_);
        pack_ns += ToNs(Clock::now() - pack_begin);

        smgr_->writeSSTable(static_cast<uint8_t>(srcLevel_ + 1),
                            minK, maxK, std::move(buffer),
                            /*clearImmuteTable=*/false);

        while (!sortedList_.empty()) sortedList_.pop();
        nums_ = 0;
        if (packType_ == PackingType::kHash) {
            std::fill(hash_num_.begin(), hash_num_.end(), 0);
        }
        return Status::OK();
    };

    auto emit = [&](std::string_view k) -> Status {
        InternalKey a = InternalKey::Decode(std::string(k.data(), k.size()));
        if (a.key.key_size == 0) {
            pr_error("Compaction sorted list insert a error internal key");
            a.dump();
            return Status::OK();
        }

        sortedList_.emplace(k.data(), k.size());

        if (packType_ == PackingType::kHash) {
            InternalKey key{};
            if (!DecodeInternal(k, key)) {
                return Status::IOError("emit: bad internal key (hash)");
            }
            auto idx = HashModN(key, SLOT_NUM_PER_PAGE);
            if (idx >= hash_num_.size()) {
                return Status::IOError("hash_num_ not initialized");
            }
            hash_num_[idx]++;
        } else {
            ++nums_;
        }

        if (memTableIsFull()) {
            return flush();
        }
        return Status::OK();
    };

    bool l_valid = srcLevelIter_->Valid();
    bool r_valid = dstLevelIter_->Valid();

    std::string last_user_key;
    bool have_last = false;

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
                pr_error("both keys decode failed; default take_left");
                take_left = true;
            } else if (!rd) {
                take_left = true;
            } else if (!ld) {
                take_left = false;
            } else {
                take_left = (*icmp_)(lk, rk);
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
            if (has_prev &&
                !(*icmp_)(prev_internal, cur_internal) &&
                !equal_internal(prev_internal, cur_internal)) {
                pr_error("Compaction non-monotonic: previous > current (internal order broken)");
            }
            if (cur_internal.key.key_size == 0) {
                pr_error("Internal key is error");
                continue;
            }
            prev_internal = cur_internal;
            has_prev = true;
        }

        std::string cur_user;
        if (!ExtractUserKey(cur_key, cur_user)) {
            pr_error("extract_user_key failed, skip");
            continue;
        }

        if (!have_last || cur_user != last_user_key) {
            last_user_key = std::move(cur_user);
            have_last = true;
            emitted_keys++;

            auto es = emit(cur_key);
            if (!es.ok()) {
                pr_error("Compaction error 0");
                return finish(es);
            }
        } else {
            dropped_old_versions++;
        }
    }

    if (!sortedList_.empty()) {
        auto fs = flush();
        if (!fs.ok()) {
            pr_error("Compaction error 1");
            return finish(fs);
        }
    }

    const auto wait_begin = Clock::now();
    smgr_->waitAllTasksDone();
    wait_ns += ToNs(Clock::now() - wait_begin);

    return finish(Status::OK());
}
