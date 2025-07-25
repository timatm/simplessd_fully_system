#include "tree.hh"
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
extern Tree tree;

std::vector<std::shared_ptr<TreeNode>> Tree::search_overlap(int level, const Key& queryMin, const Key& queryMax) {
    std::vector<std::shared_ptr<TreeNode>> result;
    auto& nodes = level_map[level];

    auto dummy = std::make_shared<TreeNode>("dummy", level, queryMin, queryMin);
    auto it = nodes.lower_bound(dummy);

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

    return result;
}

void Tree::build_link(std::shared_ptr<TreeNode> node){
    int parent_level = node->levelInfo - 1;
    int children_level = node->levelInfo + 1;
    std::vector<std::shared_ptr<TreeNode>> Poverlap;
    std::vector<std::shared_ptr<TreeNode>> Coverlap;
    if(parent_level == 0){
        pr_debug("Parent level is 0, no parent node can be linked");
    }   
    else{
        Poverlap = search_overlap(parent_level, node->rangeMin, node->rangeMax);
    }
    if(children_level >= MAX_LEVEL){
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
    std::vector<std::shared_ptr<TreeNode>> overlap = search_overlap(level, node->rangeMin, node->rangeMax);
    if(overlap.empty()){
        level_map[level].insert(node);
    }
    else{
        pr_debug("Insert node key range is error,key range overlap");
    }
    build_link(node);
}

void Tree::remove_node(std::shared_ptr<TreeNode> node){
    int level = node->levelInfo;

    auto& nodes = level_map[level];
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

std::queue<std::shared_ptr<TreeNode>> Tree::search_key(const Key& key) {
    std::queue<std::shared_ptr<TreeNode>> result;
    std::shared_ptr<TreeNode> dummy = std::make_shared<TreeNode>("dummy", 0, key, key);

    for(int i = 0; i < MAX_LEVEL; i++){
        auto& nodes = level_map[i];
        auto it = nodes.upper_bound(dummy);
        if (it != nodes.begin()) {
            --it;
            std::cout << "Check node " << (*it)->filename  << " in level:" << (*it)->levelInfo << std::endl;
            if (compareKey((*it)->rangeMin, key) <= 0 && compareKey((*it)->rangeMax, key) >= 0) {
                result.push(*it);
            }
        }
    }

    return result;
}

std::shared_ptr<TreeNode> Tree::find_node(const std::string& filename, int level, const Key& rangeMin, const Key& rangeMax) {
    auto& nodes = level_map[level];
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
    for(const auto& [level, nodes] : level_map) {
        for (const auto& node : nodes) {
            if (node->filename == filename) {
                return node;
            }
        }
    }
    return nullptr;
}

std::vector<int> Tree::get_relate_ch_info(std::shared_ptr<TreeNode> node) {
    std::vector<int> relate_ch_info(CHANNEL_NUM, 0);
    std::queue<std::shared_ptr<TreeNode>> Pqueue, Cqueue;
    std::unordered_set<TreeNode*> Pvisited, Cvisited;

    for (auto& parent : node->parent) {
        if (auto sp = parent.lock()) Pqueue.push(sp);
    }
    for (auto& child : node->children) {
        Cqueue.push(child.second);
    }

    while (!Pqueue.empty()) {
        auto parent = Pqueue.front(); 
        Pqueue.pop();
        if (!Pvisited.insert(parent.get()).second) continue;

        if (parent->channelInfo != -1) {
            relate_ch_info[parent->channelInfo]++;
        }
        for (auto& p : parent->parent) {
            if (auto sp = p.lock())
            if (compareKey(sp->rangeMin, node->rangeMax) <= 0 &&
                compareKey(sp->rangeMax, node->rangeMin) >= 0) {
                Pqueue.push(sp);
            }
        }
    }

    while (!Cqueue.empty()) {
        auto child = Cqueue.front();
        Cqueue.pop();
        if (!Cvisited.insert(child.get()).second) continue;

        if (child->channelInfo != -1) {
            relate_ch_info[child->channelInfo]++;
        }
        for (auto& [filename, c] : child->children) {
            if (!c){
                pr_debug("Filename: %s can't find pointer", filename);
                continue;
            }
            if (compareKey(c->rangeMin, node->rangeMax) <= 0 &&
                compareKey(c->rangeMax, node->rangeMin) >= 0) {
                Cqueue.push(c);
            }
        }
    }

    auto& nodes = level_map[node->levelInfo];
    auto it = nodes.find(node);
    if (it == nodes.end()) {
        pr_debug("Node not found in level_map");
        return relate_ch_info;
    }
    if (it != nodes.begin()) {
        auto prev = std::prev(it);
        relate_ch_info[(*prev)->channelInfo]++;
    }
    if (it != nodes.end()) {
        auto next = std::next(it);
        if (next != nodes.end()) {
            relate_ch_info[(*next)->channelInfo]++;
        }
    }
    return relate_ch_info;
}

void Tree::clear() {
    size_t total = 0;
    for (const auto& [level, nodes] : level_map) {
        total += nodes.size();
    }
    pr_info("Tree clear: releasing %zu nodes", total);

    for (auto& [level, nodes] : level_map) {
        nodes.clear();
    }
    level_map.clear();
}

void Tree::dump() const {
    std::cout << "=== Tree Dump ===" << std::endl;

    for (int level = 0; level <= 7; ++level) {
        auto it = level_map.find(level);
        if (it == level_map.end() || it->second.empty()) continue;

        std::cout << "Level " << level << ":\n";
        for (const auto& node : it->second) {
            std::cout << "  - " << node->filename << " | "
                      << "rangeMin: " << node->rangeMin.toString()
                      << ", rangeMax: " << node->rangeMax.toString() << "\n";
        }
    }

    std::cout << "=================\n";
}
