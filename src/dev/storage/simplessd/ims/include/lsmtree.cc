#include "lsmtree.hh"
struct FilenameLess {
    bool operator()(const std::shared_ptr<TreeNode>& a,
                    const std::shared_ptr<TreeNode>& b) const {
        return a->filename > b->filename; 
    }
};


std::queue<std::shared_ptr<TreeNode>> LSMTree::search_key(const Key& key) {
    auto dummy = std::make_shared<TreeNode>("dummy", 0, key, key);
    std::queue<std::shared_ptr<TreeNode>> result;
    std::set<std::shared_ptr<TreeNode>,FilenameLess> level0_candidates;
    const auto& nodes = tree_->get_level_nodes(0);
    for(const auto& node:nodes){
        if (compareKey(node->rangeMin, key) <= 0 && 
            compareKey(node->rangeMax, key) >= 0){
            node->dump();
            level0_candidates.insert(node);
        }
    }
    for(const auto& candidate : level0_candidates){
        result.push(candidate);
    }
    for (int level = 1; level < MAX_LEVEL; ++level) {
        const auto& nodes = tree_->get_level_nodes(level);
        if (nodes.empty()) continue;

        
        auto it = nodes.upper_bound(dummy);

        if (it != nodes.begin()) {
            --it;
            const auto& candidate = *it;
            std::cout << "Check node " << candidate->filename << " in level: " << level << std::endl;

            if (compareKey(candidate->rangeMin, key) <= 0 &&
                compareKey(candidate->rangeMax, key) >= 0) {
                candidate->dump();
                result.push(candidate);
            }
        }
    }

    return result;
}


std::vector<int> LSMTree::get_relate_ch_info(std::shared_ptr<TreeNode> node) {
    std::vector<int> relate_ch_info(CHANNEL_NUM, 0);
    std::queue<std::shared_ptr<TreeNode>> Pqueue, Cqueue;
    std::unordered_set<TreeNode*> Pvisited, Cvisited;

    // 處理 parent 節點
    for (auto& parent : node->parent) {
        if (auto sp = parent.lock()) {
            Pqueue.push(sp);
        }
    }

    while (!Pqueue.empty()) {
        auto parent = Pqueue.front(); 
        Pqueue.pop();
        if (!Pvisited.insert(parent.get()).second) continue;

        if (parent->channelInfo >= 0) {
            relate_ch_info[parent->channelInfo]++;
        }

        for (auto& gp : parent->parent) {
            if (auto sp = gp.lock()) {
                if (compareKey(sp->rangeMin, node->rangeMax) <= 0 &&
                    compareKey(sp->rangeMax, node->rangeMin) >= 0) {
                    Pqueue.push(sp);
                }
            }
        }
    }

    // 處理 child 節點
    for (auto& [_, child] : node->children) {
        Cqueue.push(child);
    }

    while (!Cqueue.empty()) {
        auto child = Cqueue.front();
        Cqueue.pop();
        if (!Cvisited.insert(child.get()).second) continue;

        if (child->channelInfo >= 0) {
            relate_ch_info[child->channelInfo]++;
        }

        for (auto& [filename, grandchild] : child->children) {
            if (!grandchild) {
                pr_debug("Filename: %s can't find pointer", filename.c_str());
                continue;
            }
            if (compareKey(grandchild->rangeMin, node->rangeMax) <= 0 &&
                compareKey(grandchild->rangeMax, node->rangeMin) >= 0) {
                Cqueue.push(grandchild);
            }
        }
    }

    // 加入同層相鄰節點資訊
    int level = node->levelInfo;
    if (level < 0 || level >= MAX_LEVEL) {
        pr_debug("Invalid node level: %d", level);
        return relate_ch_info;
    }

    const auto& nodes = tree_->get_level_nodes(level);
    auto it = nodes.find(node);
    if (it == nodes.end()) {
        pr_debug("Node not found in level_map");
        return relate_ch_info;
    }

    if (it != nodes.begin()) {
        auto prev = std::prev(it);
        if ((*prev)->channelInfo >= 0) {
            relate_ch_info[(*prev)->channelInfo]++;
        }
    }

    auto next = std::next(it);
    if (next != nodes.end() && (*next)->channelInfo >= 0) {
        relate_ch_info[(*next)->channelInfo]++;
    }

    return relate_ch_info;
}
// 插入一個 SSTable 對應的 TreeNode
void LSMTree::insert_sstable(std::shared_ptr<TreeNode> node) {
    tree_->insert_node(node);
}

// 移除一個 SSTable 對應的 TreeNode
void LSMTree::remove_sstable(std::shared_ptr<TreeNode> node) {
    tree_->remove_node(node);
}

// 搜尋特定完整資訊的 TreeNode
std::shared_ptr<TreeNode> LSMTree::find_node(const std::string& filename, int level, const Key& min, const Key& max) {
    return tree_->find_node(filename, level, min, max);
}

// 搜尋僅由 filename 確認的 TreeNode
std::shared_ptr<TreeNode> LSMTree::find_node(const std::string& filename) {
    return tree_->find_node(filename);
}