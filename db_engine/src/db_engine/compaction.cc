#include "compaction.hh"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <exception>

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
    return static_cast<uint64_t>(std::chrono::duration_cast<Ns>(d).count());
}

static inline bool DecodeInternal(std::string_view s, InternalKey& out) {
    if (s.size() != sizeof(InternalKey)) {
        pr_error("DecodeInternal: bad size=%zu (expect=%zu)",
                 s.size(), sizeof(InternalKey));
        return false;
    }
    out = InternalKey::Decode(s.data());
    return true;
}

static inline bool SameUserKey(const InternalKey& ik,
                               const char* last_key,
                               uint8_t last_key_size) {
    return ik.key.key_size == last_key_size &&
           std::memcmp(ik.key.key, last_key, last_key_size) == 0;
}
}  // namespace

CompactionRunner::CompactionRunner(SstableManager* smgr,
                                   LogManager* lmgr,
                                   LSMTree* tree,
                                   const InternalKeyComparator* icmp,
                                   PackingType type,
                                   int level,
                                   std::vector<std::shared_ptr<TreeNode>> srcSstables,
                                   std::vector<std::shared_ptr<TreeNode>> dstSstables,
                                   uint32_t& sstable_write_count)
    : smgr_(smgr),
      lmgr_(lmgr),
      tree_(tree),
      icmp_(icmp),
      nums_(0),
      packType_(type),
      srcLevel_(level),
      sstable_write_count_compaction(sstable_write_count) {
    if (level == 0) {
        srcLevelIter_ = std::make_unique<Level0Iterator>(
            smgr_, lmgr_, icmp_, tree_, std::move(srcSstables), true);
    } else if (level > 0 && level < MAX_LEVEL) {
        srcLevelIter_ = std::make_unique<LevelNIterator>(
            smgr_, lmgr_, icmp_, tree_, level, std::move(srcSstables), true);
    } else {
        pr_error("CompactionRunner source level is error");
    }

    if (level + 1 < MAX_LEVEL) {
        dstLevelIter_ = std::make_unique<LevelNIterator>(
            smgr_, lmgr_, icmp_, tree_, level + 1, std::move(dstSstables), true);
    } else {
        pr_error("CompactionRunner destination level is error");
    }

    batch_keys_.clear();
    batch_min_key_ = InternalKey{};
    batch_max_key_ = InternalKey{};
    batch_has_key_ = false;
    hash_num_.clear();
    if (packType_ == PackingType::kHash) {
        hash_num_.assign(SLOT_NUM_PER_PAGE, 0);
    }
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
        const uint64_t total_ns         = ToNs(Clock::now() - run_begin);
        const uint64_t materialize_ns   = g_comp_materialize_ns;
        const uint64_t write_submit_ns  = g_comp_write_ns;

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
        g_cmp_write_stage_ns.fetch_add(pack_ns + write_submit_ns,
                                       std::memory_order_relaxed);
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
                static_cast<unsigned long long>(flush_cnt),
                static_cast<unsigned long long>(emitted_keys),
                static_cast<unsigned long long>(dropped_old_versions));

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

    auto equal_internal = [&](const InternalKey& a, const InternalKey& b) -> bool {
        return std::memcmp(&a, &b, sizeof(InternalKey)) == 0;
    };

    batch_keys_.clear();
    batch_min_key_ = InternalKey{};
    batch_max_key_ = InternalKey{};
    batch_has_key_ = false;
    nums_ = 0;
    if (packType_ == PackingType::kHash) {
        std::fill(hash_num_.begin(), hash_num_.end(), 0);
    }

    auto flush = [&]() -> Status {
        if (batch_keys_.empty()) return Status::OK();

        const InternalKey minK = batch_min_key_;
        const InternalKey maxK = batch_max_key_;

        AlignedBuf buffer{};
        const auto pack_begin = Clock::now();
        try {
            buffer = smgr_->packingTable(batch_keys_);
        } catch (const std::exception& e) {
            pack_ns += ToNs(Clock::now() - pack_begin);
            return Status::IOError(std::string("packingTable failed: ") + e.what());
        } catch (...) {
            pack_ns += ToNs(Clock::now() - pack_begin);
            return Status::IOError("packingTable failed: unknown exception");
        }
        pack_ns += ToNs(Clock::now() - pack_begin);

        smgr_->writeSSTable(static_cast<uint8_t>(srcLevel_ + 1),
                            minK,
                            maxK,
                            std::move(buffer),
                            /*clearImmuteTable=*/false);

        ++flush_cnt;
        ++sstable_write_count_compaction;

        batch_keys_.clear();
        batch_min_key_ = InternalKey{};
        batch_max_key_ = InternalKey{};
        batch_has_key_ = false;
        nums_ = 0;

        if (packType_ == PackingType::kHash) {
            std::fill(hash_num_.begin(), hash_num_.end(), 0);
        }
        return Status::OK();
    };

    auto emit = [&](const InternalKey& key) -> Status {
        if (key.key.key_size == 0 || key.key.key_size > Key::MAX_KEY_BYTES) {
            pr_error("Compaction emit got invalid internal key");
            key.dump();
            return Status::OK();
        }

        if (!batch_has_key_) {
            batch_min_key_ = key;
            batch_has_key_ = true;
        }
        batch_max_key_ = key;
        batch_keys_.push_back(key);

        if (packType_ == PackingType::kHash) {
            const auto idx = HashModN(key, SLOT_NUM_PER_PAGE);
            if (idx >= hash_num_.size()) {
                return Status::IOError("hash_num_ not initialized");
            }
            ++hash_num_[idx];
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

    InternalKey l_cur{};
    InternalKey r_cur{};

    auto load_left = [&]() {
        while (l_valid) {
            if (DecodeInternal(srcLevelIter_->key(), l_cur)) return;
            pr_error("bad source internal key, skip one entry");
            srcLevelIter_->Next();
            l_valid = srcLevelIter_->Valid();
        }
    };

    auto load_right = [&]() {
        while (r_valid) {
            if (DecodeInternal(dstLevelIter_->key(), r_cur)) return;
            pr_error("bad destination internal key, skip one entry");
            dstLevelIter_->Next();
            r_valid = dstLevelIter_->Valid();
        }
    };

    if (l_valid) load_left();
    if (r_valid) load_right();

    char last_user_key[Key::MAX_KEY_BYTES] = {0};
    uint8_t last_user_key_size = 0;
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
            take_left = (*icmp_)(l_cur, r_cur);
        }

        const InternalKey& cur_internal = take_left ? l_cur : r_cur;

        if (has_prev && (*icmp_)(cur_internal, prev_internal) &&
            !equal_internal(prev_internal, cur_internal)) {
            pr_error("Compaction non-monotonic: previous > current");
        }

        if (cur_internal.key.key_size == 0 ||
            cur_internal.key.key_size > Key::MAX_KEY_BYTES) {
            pr_error("Internal key is invalid");
        } else {
            prev_internal = cur_internal;
            has_prev = true;

            if (!have_last ||
                !SameUserKey(cur_internal, last_user_key, last_user_key_size)) {
                std::memcpy(last_user_key,
                            cur_internal.key.key,
                            cur_internal.key.key_size);
                last_user_key_size = cur_internal.key.key_size;
                have_last = true;
                ++emitted_keys;

                auto es = emit(cur_internal);
                if (!es.ok()) {
                    pr_error("Compaction emit failed");
                    return finish(es);
                }
            } else {
                ++dropped_old_versions;
            }
        }

        if (take_left) {
            srcLevelIter_->Next();
            l_valid = srcLevelIter_->Valid();
            if (l_valid) load_left();
        } else {
            dstLevelIter_->Next();
            r_valid = dstLevelIter_->Valid();
            if (r_valid) load_right();
        }
    }

    if (!batch_keys_.empty()) {
        auto fs = flush();
        if (!fs.ok()) {
            pr_error("Compaction final flush failed");
            return finish(fs);
        }
    }

    const auto wait_begin = Clock::now();
    smgr_->waitAllTasksDone();
    wait_ns += ToNs(Clock::now() - wait_begin);

    return finish(Status::OK());
}
