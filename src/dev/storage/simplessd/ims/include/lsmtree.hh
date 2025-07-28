#ifndef __TABLE_MANAGER__HH__
#define __TABLE_MANAGER__HH__

#include "tree.hh"

class LSMTree {
public:
    explicit LSMTree(std::shared_ptr<Tree> tree)
        : tree_(std::move(tree)) {}

    std::queue<std::shared_ptr<TreeNode>> search_key(const Key& key);
    std::vector<int> get_relate_ch_info(std::shared_ptr<TreeNode> node);

    // 延伸行為建議：
    void insert_sstable(std::shared_ptr<TreeNode> node); 
    void remove_sstable(std::shared_ptr<TreeNode> node);     
    std::shared_ptr<TreeNode> find_node(const std::string& filename, int level, const Key& min, const Key& max);
    std::shared_ptr<TreeNode> find_node(const std::string& filename);
private:
    std::shared_ptr<Tree> tree_;
};


#endif