#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <queue>
#include <optional>
#include <cassert>
#include <cstring>
#include <cstdint>

// 你的專案內部型別
#include "status.hh"
#include "sstable_mgr.hh"
#include "log_manager.hh"
#include "level_iter.hh"   // 假定內含：InternalKey / InternalKeyComparator / SstableIterator / LSMTree

// ------------------------------
// 若你的專案「尚未」定義 Options / L0FileMeta，開放下列最小版供使用；
// 如果你已經有它們，請刪除或改名這兩個結構，避免重複定義。
// ------------------------------

// 迭代查詢用的上下界（*InternalKey* 編碼的 64B 字串；半開區間 [lower, upper)）
// 若你目前是 UserKey，請在外部先轉成 InternalKey 的「哨兵」再塞進來。
struct Options {
    // 64B InternalKey 的編碼（Encode 後的 fixed 64 bytes）
    std::optional<std::string> lower;  // >= lower
    std::optional<std::string> upper;  // <  upper
};

// L0 檔案的基本中繼資料（供篩選/排序/開檔）
struct L0FileMeta {
    std::string filename;      // 檔名或路徑
    std::string min_key;       // 檔內最小 InternalKey（64B 編碼）
    std::string max_key;       // 檔內最大 InternalKey（64B 編碼）
    uint64_t    entries{0};    // 可選：記錄數量
    uint64_t    file_id{0};    // 用來做 tie-break（愈新/愈舊依你的需求）
    // int      packing{0};    // 若你有壓縮/打包型別，可在此加入
};

// 假設 InternalKey 為固定 64B
static_assert(sizeof(InternalKey) == 64, "InternalKey must be 64B");

// ==============================
//          Iterator
// ==============================
class Level0Iterator :public InternalIterator{
public:
    Level0Iterator( SstableManager* smgr,
                    LogManager*     lmgr,
                    const InternalKeyComparator* icmp,
                    LSMTree*        tree,
                    Options         opts,
                    bool            compaction);
    Level0Iterator( SstableManager* smgr,
                    LogManager*     lmgr,
                    const InternalKeyComparator* icmp,
                    LSMTree*        tree,
                    std::vector<std::shared_ptr<TreeNode>> meta,
                    bool            IsCompaction);
    // 基本 API
    Status Init() override;                    // 一次性開啟所有 L0 檔案
    bool   Valid() const override;
    void   SeekToFirst() override;
    void   SeekToLast() override;
    void   Seek(std::string_view internal_target) override;
    void   Next() override;
    void   Prev() override;

    std::string_view key() const override;
    bool SupportsValueCopy() const override { return true; }
    Status ReadValue(std::string& out) const override;
    Status status() const override;
    
private:
    struct Child {
        L0FileMeta meta{};
        std::unique_ptr<SstableIterator> it;
        bool in_heap{false};
        bool opened{false};
    };

    // 最小堆（top = 最小 key）
    struct HeapCmp {
        const InternalKeyComparator* icmp;
        const std::vector<Child>*    children;

        bool operator()(size_t a, size_t b) const;
        static bool LessKey(const InternalKeyComparator& ic,
                            std::string_view a, std::string_view b);
    };
    using Heap = std::priority_queue<size_t, std::vector<size_t>, HeapCmp>;

private:
    // ---- 內部 helper ----
    Status open_all_children_();                 // 一次性開所有 L0 檔案
    void   clear_heap_();                        // 清空堆與 in_heap 標誌
    void   push_heap_(size_t i);                 // 若不在堆就推入
    void   pull_top_();                          // 設定目前最小 key（forward）
    void   pull_top_max_();                      // 設定目前最大 key（backward）
    bool   within_upper_(std::string_view k) const;
    bool   ge_lower_(std::string_view k) const;
    bool   LessKey_(std::string_view a, std::string_view b) const;
    bool   LessKey_(const std::string& a, const std::string& b) const;
    bool   EqualKey_(std::string_view a, std::string_view b) const;

    // 區間與交集判斷（用於過濾 child 是否可能命中）
    bool   overlap_with_range_(const L0FileMeta& fm) const;
    bool   overlap_with_range_with_target_(const L0FileMeta& fm, std::string_view target) const;

    // 從你的系統取 L0 檔案清單（在 .cc 實作）
    bool   LoadL0Metas_();

    // 把 opts_ 轉成 canonical internal bounds（若已是 InternalKey 就直接沿用）
    void   build_bounds_from_opts_();
    
private:
    SstableManager* smgr_;
    LogManager*     lmgr_;
    LSMTree*        tree_;
    const InternalKeyComparator* icmp_;
    Options         opts_;

    std::vector<L0FileMeta> metas_;
    std::vector<Child>      children_;

    // canonical internal bounds（半開區間 [lower, upper)）
    std::optional<std::string> canon_lower_;
    std::optional<std::string> canon_upper_;

    Heap            heap_;
    bool            has_top_{false};
    std::string_view key_;
    size_t          curr_idx_{static_cast<size_t>(-1)}; // ReadValue 時的真實來源 child
    Status          st_{Status::OK()};
    bool            compaction_;
};




class LevelNIterator :public InternalIterator{
public:
    LevelNIterator( SstableManager* smgr,
                    LogManager* lmgr,
                    const InternalKeyComparator* icmp,
                    LSMTree* tree,
                    int level, // 1..6
                    Options opts);


    LevelNIterator( SstableManager* smgr,
                    LogManager* lmgr,
                    const InternalKeyComparator* icmp,
                    LSMTree* tree,
                    int level, // 1..6
                    std::vector<std::shared_ptr<TreeNode>> meta,
                    bool compaction
                    );


    // 基本 API（語義與 Level0Iterator 相同）
    Status Init() override;
    bool Valid() const override;
    void SeekToFirst() override;
    void SeekToLast() override;
    void Seek(std::string_view internal_target) override;
    void Next() override;
    void Prev() override;


    std::string_view key() const override;
    bool SupportsValueCopy() const override { return true; };
    Status ReadValue(std::string& out) const override;
    Status status() const override;


    // 設定同時最大開檔數（預設 64）。可在 Init() 前設定。
    void set_max_open_children(size_t n) { max_open_children_ = (n == 0 ? 1 : n); }


private:
    struct Child {
        L0FileMeta meta{};
        std::unique_ptr<SstableIterator> it;
        bool opened{false};
    };

    // ---- 內部 helper ----
    void reset_view_(); // 重置當前狀態
    bool within_upper_(std::string_view k) const;
    bool ge_lower_(std::string_view k) const;
    bool LessKey_(std::string_view a, std::string_view b) const;
    bool LessKey_(const std::string& a, const std::string& b) const;


    // 把 opts_ 轉成 canonical internal bounds（若已是 InternalKey 就直接沿用）
    void build_bounds_from_opts_();


    // 從系統讀取某 level 的 metas（依 lower/upper 篩選）；同時會排序 metas_ by min_key
    bool LoadLevelMetas_();


    // 針對某個 child ，根據 lower/upper 把 iterator 放到「第一個可用」位置
    bool position_child_to_first_(size_t i); // 用在 SeekToFirst/換檔往前
    bool position_child_to_last_(size_t i); // 用在 SeekToLast/換檔往後
    bool position_child_to_target_ge_(size_t i, std::string_view target); // 用在 Seek(target)


    // 在 metas_ 中找到「第一個可能 ≥ lower」的檔案索引（若無 lower 就回傳 0）
    size_t find_first_file_ge_lower_() const;
    // 在 metas_ 中找到「最後一個可能 < upper」的檔案索引（若無 upper 就回傳 metas_.size()-1）
    size_t find_last_file_lt_upper_() const;
    // 針對 target 找到可能包含 target 的檔案（min_key ≤ target ≤ max_key）不存在則回 metas_.size()
    size_t find_file_for_target_(std::string_view target) const;


    // ---- Lazy open + LRU 關檔 ----
    Status ensure_child_open_(size_t i);
    void lru_touch_(size_t i);
    void lru_evict_if_needed_();


private:
    SstableManager* smgr_;
    LogManager*     lmgr_;
    const InternalKeyComparator *icmp_;
    LSMTree*        tree_;
    const int level_; // 1..6
    Options opts_;


    std::vector<L0FileMeta> metas_; // 該 level 的檔案（已依 min_key 排序）
    std::vector<Child> children_;


    // canonical internal bounds（半開區間 [lower, upper)），固定 64B internal-key 編碼
    std::optional<std::string> canon_lower_;
    std::optional<std::string> canon_upper_;

    bool compaction_;
    // 當前檔與 key 檢視
    size_t cur_file_{static_cast<size_t>(-1)};
    std::string_view key_;
    bool has_top_{false};
    Status st_{Status::OK()};


    // Lazy open + LRU
    size_t max_open_children_{64};
    std::deque<size_t> open_lru_;
};