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
            // node->dump();
            level0_candidates.insert(node);
        }
    }
    for(const auto& candidate : level0_candidates){
        result.push(candidate);
    }
    for (int level = 1; level < MAX_LEVEL; ++level) {
        const auto& Lnodes = tree_->get_level_nodes(level);
        if (Lnodes.empty()) continue;

        
        auto it = Lnodes.lower_bound(dummy);  // 注意：改用 lower_bound 較直覺

        if (it != Lnodes.end()) {
            const auto& cand = *it;
            if (compareKey(cand->rangeMin, key) <= 0 &&
                compareKey(cand->rangeMax,  key) >= 0) {
                result.push(cand);
            }
        }
        if (it != Lnodes.begin()) {
            auto it_prev = std::prev(it);
            const auto& cand = *it_prev;
            if (compareKey(cand->rangeMin, key) <= 0 &&
                compareKey(cand->rangeMax,  key) >= 0) {
                result.push(cand);
            }
        }

    }

    return result;
}


std::vector<int> LSMTree::get_relate_ch_info(std::shared_ptr<TreeNode> node) {
    std::vector<int> relate_ch_info(CHANNEL_NUM, 0);
    std::queue<std::shared_ptr<TreeNode>> Pqueue, Cqueue;
    std::unordered_set<TreeNode*> Pvisited, Cvisited;

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
void LSMTree::insert_sstable(std::shared_ptr<TreeNode> node) {
    tree_->insert_node(node);
    int level = node->levelInfo;
    level_num_[level]++;
}

void LSMTree::remove_sstable(std::shared_ptr<TreeNode> node) {
    tree_->remove_node(node);
    int level = node->levelInfo;
    level_num_[level]--;
}

std::shared_ptr<TreeNode> LSMTree::find_node(const std::string& filename, int level, const Key& min, const Key& max) {
    return tree_->find_node(filename, level, min, max);
}

std::shared_ptr<TreeNode> LSMTree::find_node(const std::string& filename) {
    return tree_->find_node(filename);
}


std::vector<std::vector<std::shared_ptr<TreeNode>>> LSMTree::search_all_level(const Key& queryMin, const Key& queryMax){
    std::vector<std::vector<std::shared_ptr<TreeNode>>> result(MAX_LEVEL);
    for(int level = 0;level < MAX_LEVEL;level++){
        result[level] = tree_->search_overlap(level,queryMin,queryMax);
    }
    return result;
}

std::vector<std::shared_ptr<TreeNode>> LSMTree::search_one_level(int level,const Key& queryMin, const Key& queryMax){
    std::vector<std::shared_ptr<TreeNode>> result;
    result = tree_->search_overlap(level,queryMin,queryMax);
    return result;
}

std::shared_ptr<TreeNode> LSMTree::getLevelFirstNode(int level) const {
    const auto& nodes = tree_->get_level_nodes(level);
    if (!nodes.empty()) {
        return *nodes.begin();
    }
    return nullptr;
}

std::shared_ptr<TreeNode> LSMTree::getNextNode(int level, Key input) const{
    const auto& nodes = tree_->get_level_nodes(level);
    if (nodes.empty()) {
        throw std::runtime_error("Level is empty");
    }

    auto dummy = std::make_shared<TreeNode>("dummy", level, input, input);
    auto it = nodes.upper_bound(dummy);

    if (it != nodes.end()) {
        return *it;
    }
    return *nodes.begin();
}

std::shared_ptr<TreeNode> LSMTree::findLevel0Older(){
    const auto& nodes = tree_->get_level_nodes(0);
    if (nodes.empty()) {
        return nullptr;
    }
    auto oldest = *nodes.begin();
    for (const auto& node : nodes) {
        if (node->filename < oldest->filename) {
            oldest = node;
        }
    }
    return oldest;
}

std::vector<std::shared_ptr<TreeNode>> LSMTree::get_level_treeNode(int level){

    std::vector<std::shared_ptr<TreeNode>> result;
    if(level < 0 || level > MAX_LEVEL){
        pr_debug("Error level in get_level_treeNode()");
        return result;
    }
    auto nodes = tree_->get_level_nodes(level);
    for(auto node : nodes){
        result.emplace_back(node);
    }
    return result;
}