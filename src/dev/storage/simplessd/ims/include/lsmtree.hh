#ifndef __TABLE_MANAGER__HH__
#define __TABLE_MANAGER__HH__

#include <string>
#include <vector>
#include "tree.hh"



class LSMTree {
public:
    explicit LSMTree(std::shared_ptr<Tree> tree): tree_(std::move(tree)) {
        level_num_.resize(MAX_LEVEL, 0);
    }
    LSMTree(){
        tree_ = std::make_shared<Tree>();
    }
    std::queue<std::shared_ptr<TreeNode>> search_key(const Key& key);
    RelateChInfo get_relate_ch_info(std::shared_ptr<TreeNode> node);

    void insert_sstable(std::shared_ptr<TreeNode> node); 
    void remove_sstable(std::shared_ptr<TreeNode> node);     
    std::shared_ptr<TreeNode> find_node(const std::string& filename, int level, const Key& min, const Key& max);
    std::shared_ptr<TreeNode> find_node(const std::string& filename);
    void dump_lsmtere() const {
        tree_->dump();
    }
    std::vector<std::shared_ptr<TreeNode>> get_level_treeNode(int level);
    std::vector<std::vector<std::shared_ptr<TreeNode>>> search_all_level(const Key& queryMin, const Key& queryMax);
    std::vector<std::shared_ptr<TreeNode>> search_one_level(int level,const Key& queryMin, const Key& queryMax);
    std::string encode() const{return tree_->encode();};
    bool decode(const std::string& buf){
        tree_->clear();
        return tree_->decode(buf);
    };
    uint32_t get_level_num(int level) const {
        if (level < 0 || level >= MAX_LEVEL) return 0;
        return level_num_[level];
    }
    void clear(){
        tree_->clear();
        std::fill(level_num_.begin(), level_num_.end(), 0);
    };
    std::shared_ptr<TreeNode> findLevel0Older();
    std::shared_ptr<TreeNode> getLevelFirstNode(int level) const ;
    std::shared_ptr<TreeNode> getNextNode(int level, Key input) const;
private:
    std::shared_ptr<Tree> tree_;
    std::vector<uint32_t> level_num_;
};


#endif 