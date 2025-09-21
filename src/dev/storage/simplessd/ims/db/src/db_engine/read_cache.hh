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
#include <memory>
#include "options.hh"
#include "internal_key.hh"
#include "status.hh"

class ReadCache {
public:
    explicit ReadCache(size_t capacity);
    explicit ReadCache();
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


// class TableCache {
// public:
//     explicit TableCache(size_t capacity) : capacity_(capacity) {}

//     std::shared_ptr<TableReader> Get(const std::string& file,
//                                      SstableManager& mgr,
//                                      const InternalKeyComparator& icmp) {
//         std::lock_guard<std::mutex> lk(mu_);
//         if (auto it = map_.find(file); it != map_.end()) {
//             touch(it->second);
//             return it->second.reader;
//         }
//         // miss
//         auto reader = std::make_shared<TableReader>(file, mgr, icmp);
//         auto st = reader->Init();
//         if (!st.ok()) return nullptr;

//         if (list_.size() == capacity_) evict_one();
//         Node node{file, reader};
//         list_.push_front(node);
//         map_[file] = list_.begin();
//         return reader;
//     }

//     void Erase(const std::string& file) {
//         std::lock_guard<std::mutex> lk(mu_);
//         if (auto it = map_.find(file); it != map_.end()) {
//             list_.erase(it->second);
//             map_.erase(it);
//         }
//     }

// private:
//     struct Node {
//         std::string file;
//         std::shared_ptr<TableReader> reader;
//     };
//     void touch(std::list<Node>::iterator it) {
//         list_.splice(list_.begin(), list_, it); // move to front
//     }
//     void evict_one() {
//         auto& back = list_.back();
//         map_.erase(back.file);
//         list_.pop_back();
//     }

//     size_t capacity_;
//     std::list<Node> list_;
//     std::unordered_map<std::string, std::list<Node>::iterator> map_;
//     std::mutex mu_;
// };



#endif  // __READ_CACHE_HH__