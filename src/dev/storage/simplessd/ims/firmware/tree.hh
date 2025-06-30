#ifndef __TREE_H__
#define __TREE_H__

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <set>

#include <queue>
#include "print.hh"
#include "def.hh"
#include "lbn_pool.hh"
#include "IMS_interface.hh"
#include "mapping_table.hh"

struct TreeNode : public std::enable_shared_from_this<TreeNode>{
    std::string filename;
    int levelInfo;
    int channelInfo;
    // std::string rangeMin;
    // std::string rangeMax;
    // std::unordered_map<std::string, std::shared_ptr<TreeNode> > children;
    // std::vector<std::weak_ptr<TreeNode>> parent;
    // TreeNode(std::string name, int level,int ch,std::string min, std::string max):
    //     filename(std::move(name)),
    //     levelInfo(level),
    //     channelInfo(ch),
    //     rangeMin(min),
    //     rangeMax(max){}
    // TreeNode(std::string name, int level,std::string min, std::string max):
    //     TreeNode(std::move(name), level, -1, std::move(min), std::move(max)){}

    int rangeMin;
    int rangeMax;
    std::unordered_map<std::string, std::shared_ptr<TreeNode> > children;
    // Weak_ptr cna't be used in unordered_map, so we use vetor to contain parent nodes
    std::vector<std::weak_ptr<TreeNode>> parent;
    TreeNode(std::string name, int level,int ch,int min, int max):
        filename(std::move(name)),
        levelInfo(level),
        channelInfo(ch),
        rangeMin(min),
        rangeMax(max){}
    TreeNode(std::string name, int level,int min, int max):
        TreeNode(std::move(name), level,INVALIDCH, min,max){}

    ~TreeNode() {
        // std::cout << "TreeNode " << filename << " is destroyed\n";
    }
};
struct TreeNodeComparator {
    bool operator()(const std::shared_ptr<TreeNode>& a,
                    const std::shared_ptr<TreeNode>& b) const {
        if (a->rangeMin != b->rangeMin)
            return a->rangeMin < b->rangeMin;
        if (a->rangeMax != b->rangeMax)
            return a->rangeMax < b->rangeMax;
        return a.get() < b.get();  // 保證即使 range 一樣也不會視為重複
    }
};
class Tree {
public: 
    std::unordered_map<int, std::set<std::shared_ptr<TreeNode>, TreeNodeComparator>> level_map;
    int init_tree();
    void insert_node(std::shared_ptr<TreeNode> node);
    void remove_node(std::shared_ptr<TreeNode> node);
    std::queue<std::shared_ptr<TreeNode>> search_key(int key);

    std::vector<std::shared_ptr<TreeNode>> search_overlap(int level, int queryMin, int queryMax);
    void build_link(std::shared_ptr<TreeNode> node);

    std::shared_ptr<TreeNode> find_node(std::string filename,int level,int ,int);
    std::shared_ptr<TreeNode> find_node(std::string filename);
    std::vector<int> get_relate_ch_info(std::shared_ptr<TreeNode> node);
    // TODO release memory of the tree
    void clear();
};


extern Tree tree;

#endif