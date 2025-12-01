#include "tree.hh"
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
extern Tree tree;

std::vector<std::shared_ptr<TreeNode>> Tree::search_overlap(int level, const Key& queryMin, const Key& queryMax) {
    std::vector<std::shared_ptr<TreeNode>> result;
   
    auto itLevel = level_map_.find(level);
    if (itLevel == level_map_.end()) return result;
    const auto& nodes = itLevel->second;

    auto dummy = std::make_shared<TreeNode>("dummy", level, queryMin, queryMin);
    auto it = nodes.lower_bound(dummy);
    if(level == 0){
        for(auto node:nodes){
            if((compareKey((node)->rangeMax, queryMin) >= 0) && (compareKey(queryMax, (node)->rangeMin) >= 0)){
                result.push_back(node);
            }
        }
    }
    else if(level > 0 && level < MAX_LEVEL){
        if (it != nodes.begin()) {
            auto prev = std::prev(it);
            if (compareKey((*prev)->rangeMax, queryMin) >= 0) {
                result.push_back(*prev);
            }
        }

        while (it != nodes.end() && compareKey((*it)->rangeMin, queryMax) <= 0) {
            if (compareKey((*it)->rangeMax, queryMin) >= 0) {
                result.push_back(*it);
            }
            ++it;
        }
    }
    
    return result;
}

void Tree::build_link(std::shared_ptr<TreeNode> node){
    int parent_level = node->levelInfo - 1;
    int children_level = node->levelInfo + 1;
    if (children_level >= MAX_LEVEL) {
        pr_debug("Children level is too high...");
        return;
    } 
    std::vector<std::shared_ptr<TreeNode>> Poverlap;
    std::vector<std::shared_ptr<TreeNode>> Coverlap;
    if(parent_level > 0){
        Poverlap = search_overlap(parent_level, node->rangeMin, node->rangeMax);
    }
    if(children_level > MAX_LEVEL){
        pr_debug("Children level is too high, no children node can be linked");
    }
    else{
        Coverlap = search_overlap(children_level, node->rangeMin, node->rangeMax);
    }

    for(const auto& parent : Poverlap){
        if(parent->children.find(node->filename) == parent->children.end()){
            parent->children[node->filename] = node;
            node->parent.push_back(parent);
        }
    }
    for (const auto& child : Coverlap) {
        bool found = false;
        for (const auto& parent : child->parent) {
            if (auto sp = parent.lock(); sp && sp.get() == node.get()) {
                found = true;
                break;
            }
        }
        if (!found) {
            child->parent.push_back(node);
            if (node->children.find(child->filename) == node->children.end()) {
                node->children[child->filename] = child;
            }
        }
    }
}

void Tree::insert_node(std::shared_ptr<TreeNode> node){
    int level = node->levelInfo;
    if(level < 0 || level > MAX_LEVEL) {
        pr_debug("Invalid level: %d", level);
        return;
    }
    std::vector<std::shared_ptr<TreeNode>> overlap;
    level_map_[level].insert(node);
    // if(level > 0){
    //     overlap = search_overlap(level, node->rangeMin, node->rangeMax);
    // }
    
    // if(overlap.empty()){
    //     level_map_[level].insert(node);
    // }
    // else{
    //     pr_debug("Insert node key range is error,key range overlap");
    // }
    build_link(node);
}

void Tree::remove_node(std::shared_ptr<TreeNode> node){
    int level = node->levelInfo;

    auto& nodes = level_map_[level];
    nodes.erase(node);

    for (auto& weak_parent : node->parent) {
        if (auto parent = weak_parent.lock()) {
            parent->children.erase(node->filename);
        }
    }
    for (auto& [filename, child] : node->children) {
        for (auto it = child->parent.begin(); it != child->parent.end(); ) {
            if (auto sp = it->lock(); sp.get() == node.get()) {
                it = child->parent.erase(it);
            } else {
                ++it;
            }
        }
    }
    node->parent.clear();
    node->children.clear();
}


std::shared_ptr<TreeNode> Tree::find_node(const std::string& filename, int level, const Key& rangeMin, const Key& rangeMax) {
    auto& nodes = level_map_[level];
    for (const auto& node : nodes) {
        if (node->filename == filename &&
            compareKey(node->rangeMin, rangeMin) == 0 &&
            compareKey(node->rangeMax, rangeMax) == 0) {
            return node;
        }
    }
    return nullptr;
}

std::shared_ptr<TreeNode> Tree::find_node(const std::string&  filename) {
    for(const auto& [level, nodes] : level_map_) {
        for (const auto& node : nodes) {
            if (node->filename == filename) {
                return node;
            }
        }
    }
    return nullptr;
}


void Tree::clear() {
    size_t total = 0;
    for (const auto& [level, nodes] : level_map_) {
        total += nodes.size();
    }
    pr_info("Tree clear: releasing %zu nodes", total);

    for (auto& [level, nodes] : level_map_) {
        nodes.clear();
    }
    level_map_.clear();
}

void Tree::dump() const {
    std::cout << "=== Tree Dump ===" << std::endl;

    for (int level = 0; level < MAX_LEVEL; ++level) {
        auto it = level_map_.find(level);
        if (it == level_map_.end() || it->second.empty()) continue;

        std::cout << "Level " << level << ":\n";
        for (const auto& node : it->second) {
            std::cout << "  - " << node->filename << " | "
                      << "rangeMin: " << node->rangeMin.toString()
                      << ", rangeMax: " << node->rangeMax.toString() << "\n";
        }
    }

    std::cout << "=================\n";
}
const std::unordered_map<int, std::set<std::shared_ptr<TreeNode>, TreeNodeComparator>>&
Tree::get_level_map() const {
    return level_map_;
}



const std::set<std::shared_ptr<TreeNode>, TreeNodeComparator>&
Tree::get_level_nodes(int level) const {
    static const std::set<std::shared_ptr<TreeNode>, TreeNodeComparator> empty;
    auto it = level_map_.find(level);
    return (it != level_map_.end()) ? it->second : empty;
}

std::string Tree::encode() const {
    std::string buffer;

    auto append_u32 = [&](uint32_t val) {
        for (int i = 0; i < 4; ++i) {
            buffer += static_cast<char>((val >> (i * 8)) & 0xFF);
        }
    };

    auto append_str = [&](const std::string& str) {
        append_u32(static_cast<uint32_t>(str.size()));
        buffer.append(str);
    };

    auto append_key = [&](const Key& key) {
        buffer += static_cast<char>(key.key_size);
        buffer.append(reinterpret_cast<const char*>(key.key), 40);
    };

    for (const auto& [level, node_set] : level_map_) {
        for (const auto& node : node_set) {
            append_str(node->filename);
            append_u32(static_cast<uint32_t>(node->levelInfo));
            append_u32(static_cast<uint32_t>(node->channelInfo));
            append_key(node->rangeMin);
            append_key(node->rangeMax);
        }
    }

    return buffer;
}

bool Tree::decode(const std::string& buf) {
    size_t offset = 0;

    auto read_u32 = [&](uint32_t& val) -> bool {
        if (offset + 4 > buf.size()) return false;
        val = 0;
        for (int i = 0; i < 4; ++i) {
            val |= static_cast<uint8_t>(buf[offset++]) << (i * 8);
        }
        return true;
    };

    auto read_str = [&](std::string& out) -> bool {
        uint32_t len = 0;
        if (!read_u32(len)) return false;
        if (offset + len > buf.size()) return false;
        out.assign(buf.data() + offset, len);
        offset += len;
        return true;
    };

    auto read_key = [&]() -> std::optional<Key> {
        if (offset + 1 + 40 > buf.size()) return std::nullopt;
        Key k = Key::decode(buf.data() + offset);
        offset += 1 + 40;
        return k;
    };

    while (offset < buf.size()) {
        std::string filename;
        uint32_t level, channel;
        Key rangeMin, rangeMax;

        if (!read_str(filename)) return false;
        if (!read_u32(level)) return false;
        if (!read_u32(channel)) return false;

        auto opt_min = read_key();
        auto opt_max = read_key();
        if (!opt_min || !opt_max) return false;

        rangeMin = *opt_min;
        rangeMax = *opt_max;

        auto node = std::make_shared<TreeNode>(filename, level, channel, rangeMin, rangeMax);
        insert_node(node);
    }

    return true;
}
