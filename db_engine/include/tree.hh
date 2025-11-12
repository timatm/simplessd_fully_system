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
#include "internal_key.hh"

inline int compareKey(const Key& a, const Key& b) {
    int cmp = std::memcmp(a.key, b.key, std::min(a.key_size, b.key_size));
    if (cmp == 0) return a.key_size - b.key_size;
    return cmp;
}

struct TreeNode : public std::enable_shared_from_this<TreeNode> {

    TreeNode(std::string name, int level, int ch, const Key& min, const Key& max)
        : filename(std::move(name)),
          levelInfo(level),
          channelInfo(ch),
          rangeMin(min),
          rangeMax(max) {}

    TreeNode(std::string name, int level, const Key& min, const Key& max)
        : TreeNode(std::move(name), level, INVALIDCH, min, max) {}

    ~TreeNode() = default;

    std::string filename;
    int levelInfo;
    int channelInfo;
    Key rangeMin;
    Key rangeMax;
    std::unordered_map<std::string, std::shared_ptr<TreeNode>> children;
    std::vector<std::weak_ptr<TreeNode>> parent;
    void dump(int indent = 0, bool recursive = false) const {
        auto pad = [indent]() { for (int i = 0; i < indent; ++i) std::cout << "  "; };

        pad(); std::cout << "TreeNode {\n";
        pad(); std::cout << "  filename    : " << filename << '\n';
        pad(); std::cout << "  levelInfo   : " << levelInfo << '\n';
        pad(); std::cout << "  channelInfo : " << channelInfo << '\n';

        pad(); std::cout << "  rangeMin    : ";
        rangeMin.dumpString();
        pad(); std::cout << "  rangeMax    : ";
        rangeMax.dumpString();

        pad(); std::cout << "  children    : " << children.size() << '\n';
        pad(); std::cout << "}\n";

        if (recursive) {
            for (const auto& sstable : children) {
                auto child = sstable.second; 
                if (child) {
                    child->dump(indent + 1, true);
                }
            }
        }
    }
};

struct TreeNodeComparator {
    bool operator()(const std::shared_ptr<TreeNode>& a,
                    const std::shared_ptr<TreeNode>& b) const {
        int cmpMin = compareKey(a->rangeMin, b->rangeMin);
        if (cmpMin != 0) return cmpMin < 0;

        int cmpMax = compareKey(a->rangeMax, b->rangeMax);
        if (cmpMax != 0) return cmpMax < 0;

        return a.get() < b.get();
    }
};



class Tree {
public:
    std::string encode() const;
    bool decode(const std::string& buf);
    int init_tree();
    void insert_node(std::shared_ptr<TreeNode> node);
    void remove_node(std::shared_ptr<TreeNode> node);
    std::vector<std::shared_ptr<TreeNode>> search_overlap(int level, const Key& queryMin, const Key& queryMax);
    std::shared_ptr<TreeNode> find_node(const std::string& filename, int level, const Key& min, const Key& max);
    std::shared_ptr<TreeNode> find_node(const std::string& filename);
    const std::unordered_map<int, std::set<std::shared_ptr<TreeNode>, TreeNodeComparator>>& get_level_map() const;
    const std::set<std::shared_ptr<TreeNode>, TreeNodeComparator>& get_level_nodes(int level) const;
    void clear();
    void dump() const;
private:
    std::unordered_map<int, std::set<std::shared_ptr<TreeNode>, TreeNodeComparator>> level_map_;
    void build_link(std::shared_ptr<TreeNode> node);
};



#endif // __TREE_H__