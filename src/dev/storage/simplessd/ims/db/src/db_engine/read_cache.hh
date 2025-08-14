#ifndef __READ_CACHE_HH__
#define __READ_CACHE_HH__

#include <cstdint>
#include <string>
#include <optional>
#include <set>
#include <cstddef>
#include <cstdio>
#include <list>
#include <unordered_map>
#include <mutex>
class ReadCache {
public:
    explicit ReadCache(size_t capacity);
    std::optional<std::set<std::string>> get(std::string&);
    bool put(const std::string&, const std::set<std::string>& value);
    bool remove(const std::string&);
    void clear();



private:
    struct Node{
        std::set<std::string> value;
        std::list<std::string>::iterator lru_it;
    };
    void evict();
    size_t capacity_;
    std::unordered_map<std::string, Node> cache_;
    std::list<std::string> lru_list_;
    mutable std::mutex mu_;

};





#endif  // __READ_CACHE_HH__