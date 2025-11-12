#include "read_cache.hh"
#include <cstdint>


ReadCache::ReadCache(size_t capacity){
    capacity_ = capacity;
};
ReadCache::ReadCache(){
    capacity_ = RANGE_KEY_CACHE_SIZE;
};

std::optional<std::set<std::string>> ReadCache::get(std::string& sstable){
    std::lock_guard<std::mutex> lk(mu_);

    auto it = cache_.find(sstable);
    if( it != cache_.end()) {
        lru_list_.erase(it->second.lru_it);
        lru_list_.push_front(sstable);
        it->second.lru_it = lru_list_.begin();
        return it->second.value;
    }
    return std::nullopt;
}



bool ReadCache::put(const std::string& sstable, const std::set<std::string>& value) {
    std::lock_guard<std::mutex> lk(mu_);

    auto it = cache_.find(sstable);
    if (it != cache_.end()) {
        it->second.value = value;                       
        lru_list_.erase(it->second.lru_it);
        lru_list_.push_front(sstable);
        it->second.lru_it = lru_list_.begin();
        return true;
    }

    if (capacity_ == 0) return false;

    if (cache_.size() >= capacity_) {
        evict(); 
    }

    Node node;
    node.value = value;
    lru_list_.push_front(sstable);
    node.lru_it = lru_list_.begin();

    cache_.emplace(sstable, std::move(node));
    return true;
}

bool ReadCache::remove(const std::string& sstable){
    std::lock_guard<std::mutex> lk(mu_);

    auto it = cache_.find(sstable);
    if (it != cache_.end()) {
        lru_list_.erase(it->second.lru_it);
        cache_.erase(it);
        return true;
    }
    return false;
}
void ReadCache::evict(){
    while (cache_.size() > capacity_) {
        if (lru_list_.empty()) return;
        auto it = std::prev(lru_list_.end());
        const std::string& victim = *it;
        cache_.erase(victim);
        lru_list_.erase(it);
    }

    return;
}
void ReadCache::clear(){
    while(!cache_.empty()){
        auto it = cache_.begin();
        lru_list_.erase(it->second.lru_it);
        cache_.erase(it);
    }
    return;
}