#ifndef __COMPACTION__HH__
#define __COMPACTION__HH__


#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <cstdint>
#include <memory>
#include <queue>

#include "status.hh"
#include "sstable_mgr.hh"
#include "log_manager.hh"
#include "level_iter.hh"
#include "db_api.hh"

static_assert(sizeof(InternalKey) == 64, "InternalKey must be fixed 64B for this scaffold");

struct CompactionPlan {
    int src_level = 0;
    int dst_level = 1;
    std::optional<std::string> lower;
    std::optional<std::string> upper;
};

// 一個極簡的 Writer 包裝：實際要呼叫 SstableManager 建立 Builder、Add(key,val)、Finish()。


class CompactionRunner {
public:
    CompactionRunner(API *db,const InternalKeyComparator* icmp,CompactionPlan config);

    // 執行 compaction，回傳新檔 metas 與統計資訊。
    Status Run();

private:
    // 2-way merge （左：src iterator，右：dst iterator），輸出到多個檔

    // 內部小工具
    bool memTableIsFull();
    static bool same_user_key(std::string_view a, std::string_view b);
    static uint8_t value_type_of(std::string_view ikey);   // 依你的 InternalKey::Decode 取 type

private:
    SstableManager* smgr_;
    LOG_MANAGER* lmgr_;
    LSMTree* tree_;
    const InternalKeyComparator* icmp_;
    size_t nums_;
    PackingType packType_;
    CompactionPlan config_;
    std::unique_ptr<InternalIterator> srcLevelIter_;
    std::unique_ptr<InternalIterator> dstLevelIter_;
    std::queue<std::string> sortedList_;
    std::vector<uint32_t> hash_num_;
};


#endif