#ifndef __COMPACTION__HH__
#define __COMPACTION__HH__

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

#include "status.hh"
#include "sstable_mgr.hh"
#include "log_manager.hh"
#include "level_iter.hh"

static_assert(sizeof(InternalKey) == 64,
              "InternalKey must be fixed 64B for this scaffold");

struct CompactionPlan {
    CompactionPlan(int level, std::string l, std::string u)
        : level_(level), lower_(std::move(l)), upper_(std::move(u)) {}

    int level_;
    std::optional<std::string> lower_;
    std::optional<std::string> upper_;
};

class CompactionRunner {
public:
    CompactionRunner(SstableManager* smgr,
                     LogManager* lmgr,
                     LSMTree* tree,
                     const InternalKeyComparator* icmp,
                     PackingType type,
                     int level,
                     std::vector<std::shared_ptr<TreeNode>> srcSstables,
                     std::vector<std::shared_ptr<TreeNode>> dstSstables,
                     uint32_t& sstable_write_count);

    Status Run();

private:
    bool memTableIsFull();

private:
    SstableManager* smgr_;
    LogManager* lmgr_;
    LSMTree* tree_;
    const InternalKeyComparator* icmp_;
    size_t nums_;
    PackingType packType_;
    int srcLevel_;
    std::unique_ptr<InternalIterator> srcLevelIter_;
    std::unique_ptr<InternalIterator> dstLevelIter_;

    std::vector<InternalKey> batch_keys_;
    InternalKey batch_min_key_;
    InternalKey batch_max_key_;
    bool batch_has_key_ = false;
    std::vector<uint32_t> hash_num_;

    uint32_t& sstable_write_count_compaction;
};

#endif
