#pragma once
#include <unordered_map>
#include <list>
#include <functional>
#include <optional>
#include <cstddef>
#include <mutex>
#include <utility>
#include <stdexcept>
#include <type_traits>

namespace cache {

// 默认的 size 計算：支援有 .size() 的型別（如 std::string / std::vector<char>）
template <typename T, typename = void>
struct DefaultSizer {
    size_t operator()(const T&) const {
        static_assert(sizeof(T) == 0, "Provide a custom sizer for this Value type");
        return 0;
    }
};

template <typename T>
struct DefaultSizer<T, std::void_t<decltype(std::declval<const T&>().size())>> {
    size_t operator()(const T& v) const { return v.size(); }
};

// ReadCache：Key/Value 任意；Value 需要可拷贝（或自定义搬移逻辑）
template <typename Key, typename Value, typename Sizer = DefaultSizer<Value>>
class ReadCache {
public:
    explicit ReadCache(size_t capacity_bytes, Sizer sizer = Sizer())
        : capacity_bytes_(capacity_bytes), size_bytes_(0), sizer_(std::move(sizer)) {}

    // 非必需，但有時你會想調整容量（會觸發必要的逐出）
    void SetCapacity(size_t capacity_bytes) {
        std::lock_guard<std::mutex> lk(mu_);
        capacity_bytes_ = capacity_bytes;
        EvictIfNeeded(/*reserve=*/0);
    }

    size_t CapacityBytes() const {
        std::lock_guard<std::mutex> lk(mu_);
        return capacity_bytes_;
    }

    size_t SizeBytes() const {
        std::lock_guard<std::mutex> lk(mu_);
        return size_bytes_;
    }

    // 命中則返回 value，否則返回空
    std::optional<Value> Get(const Key& key) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = map_.find(key);
        if (it == map_.end()) return std::nullopt;
        Touch(it); // 移到 LRU 頭部（最近使用）
        return it->second.value;
    }

    // Cache-on-read-miss：未命中用 loader 載入，並嘗試放入快取（若太大就不放）
    // 注意：loader 在鎖外執行，避免阻塞其他請求
    Value GetOrLoad(const Key& key, const std::function<Value(const Key&)>& loader) {
        // 先嘗試命中（鎖內）
        {
            std::lock_guard<std::mutex> lk(mu_);
            auto it = map_.find(key);
            if (it != map_.end()) {
                Touch(it);
                return it->second.value;
            }
        }

        // 未命中：鎖外載入
        Value loaded = loader(key);
        size_t need = sizer_(loaded);

        // 再次加鎖，雙檢避免並發重複載入造成重複插入
        {
            std::lock_guard<std::mutex> lk(mu_);
            auto it = map_.find(key);
            if (it != map_.end()) {
                Touch(it);
                return it->second.value;
            }
            // 如果單項就比整個容量還大，直接繞過快取
            if (need > capacity_bytes_) {
                return loaded;
            }
            // 騰出空間並插入
            EvictIfNeeded(need);
            InsertNoCheck(key, std::move(loaded), need);
        }
        // 這裡不能再用 moved 的 loaded，返回 map_ 內的值
        std::lock_guard<std::mutex> lk2(mu_);
        return map_.at(key).value;
    }

    // 可選：手動放入（會做 LRU 管理與逐出）
    void Put(const Key& key, Value value) {
        size_t need = sizer_(value);
        std::lock_guard<std::mutex> lk(mu_);
        if (need > capacity_bytes_) return; // 太大就不放
        auto it = map_.find(key);
        if (it != map_.end()) {
            // 更新：先扣舊大小
            size_bytes_ -= it->second.size;
            it->second.value = std::move(value);
            it->second.size = need;
            size_bytes_ += need;
            Touch(it);
            EvictIfNeeded(0);
            return;
        }
        EvictIfNeeded(need);
        InsertNoCheck(key, std::move(value), need);
    }

    // 可選：移除某 key
    bool Erase(const Key& key) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = map_.find(key);
        if (it == map_.end()) return false;
        lru_.erase(it->second.lru_it);
        size_bytes_ -= it->second.size;
        map_.erase(it);
        return true;
    }

    // 清空
    void Clear() {
        std::lock_guard<std::mutex> lk(mu_);
        map_.clear();
        lru_.clear();
        size_bytes_ = 0;
    }

private:
    struct Node {
        std::string filename;
        size_t size;
        typename std::list<Key>::iterator lru_it;
    };

    // 把 it 對應的 key 移到 LRU 頭部
    void Touch(typename std::unordered_map<Key, Node>::iterator it) {
        lru_.erase(it->second.lru_it);
        lru_.push_front(it->first);
        it->second.lru_it = lru_.begin();
    }

    // 在已經確認空間足夠時插入（不再檢查空間）
    void InsertNoCheck(const Key& key, Value value, size_t sz) {
        lru_.push_front(key);
        Node n{std::move(value), sz, lru_.begin()};
        auto [it, ok] = map_.emplace(key, std::move(n));
        (void)ok;
        size_bytes_ += sz;
    }

    // 逐出直到可以容納 reserve_bytes
    void EvictIfNeeded(size_t reserve_bytes) {
        while (size_bytes_ + reserve_bytes > capacity_bytes_ && !lru_.empty()) {
            const Key& victim_key = lru_.back();
            auto it = map_.find(victim_key);
            // 正常應該存在
            if (it != map_.end()) {
                size_bytes_ -= it->second.size;
                map_.erase(it);
            }
            lru_.pop_back();
        }
    }

    mutable std::mutex mu_;
    size_t capacity_bytes_;
    size_t size_bytes_;

    std::list<Key> lru_; // front: MRU, back: LRU
    std::unordered_map<Key, Node> map_;
    Sizer sizer_;
};

} // namespace cache
