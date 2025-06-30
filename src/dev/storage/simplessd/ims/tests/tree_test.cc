#include <gtest/gtest.h>

#include "../src/tree.hh"
#include <list>
#include <array>


void dump_all_children(const std::shared_ptr<TreeNode>& node ,std::ostream& out) {
    if (!node) {
        std::cout << "[DEBUG] Node is nullptr" << "\n";
        return;
    }
    
    if (node->children.empty()) {
        std::cout << "[DEBUG] No children for node: " << node->filename << "\n";
        return;
    }
    for (auto& [name, child] : node->children) {
        std::cout <<"[DEBUG] filename: " << node->filename << " children node:" << child->filename << " " << std::endl;
        out << child->filename << " ";
    }
    return;
}

void execute_tree_test(Tree& tree, const std::string& commands, std::ostream& out) {
    std::istringstream iss(commands);
    std::string line;
    while (std::getline(iss, line)) {
        std::istringstream linestream(line);
        std::string cmd;
        linestream >> cmd;

        if (cmd == "insert") {
            std::string filename;
            int level, min, max;
            linestream >> filename >> level >> min >> max;
            std::cout << "[DEBUG] Insert " << filename << std::endl;
            auto node = std::make_shared<TreeNode>(filename, level, min, max);
            tree.insert_node(node);
        }
        else if (cmd == "remove"){
            std::string filename;
            linestream >> filename;
            auto node = tree.find_node(filename);
            if(node){
                std::cout << "[DEBUG] remove " << filename << std::endl;
                tree.remove_node(node);
            }
            else{
                out << "not found: " << filename << "\n";
            }   
        }
        else if (cmd == "search") {
            int key;
            linestream >> key;
            auto res = tree.search_key(key);
            while (!res.empty()) {
                std::cout << res.front()->filename <<  std::endl;
                out << res.front()->filename << " ";
                res.pop();
            }
            out << "\n";
        }
        else if(cmd == "children") {
            std::string filename;
            linestream >> filename;
            auto node = tree.find_node(filename);
            if (node) {
                dump_all_children(node, out);
            } else {
                std::cout << "Node not found: " << filename << "\n";
            }
        }
        else if( cmd == "parent"){
            std::string filename;
            linestream >> filename;
            auto node = tree.find_node(filename);
            if (node) {
                for (const auto& weak_parent : node->parent) {
                    if (auto parent = weak_parent.lock()) {
                        out << parent->filename << " ";
                    }
                }
            }
            else{
                std::cout << "[DEBUG] Node not found: " << filename << "\n";
            }
        }
        else if(cmd == "searchNode"){
            std::string filename;
            auto node = tree.find_node(filename);
            if (node) {
                out << "Found node: " << node->filename << " at level " << node->levelInfo 
                    << " with range [" << node->rangeMin << ", " << node->rangeMax << "]\n";
            } else {
                out << "Node not found: " << filename << "\n";
            }
        }
        else {
            std::cout << "[DEBUG] Unknown command: " << cmd << "\n";

        }
    }
}


void reset_string(std::ostringstream& output) {
    output.str("");
    output.clear();
}
TEST(TreeTest, TreeTest1) {
    Tree tree;
    std::ostringstream output;

    std::string input = 
    R"(insert A 1 5 20
    insert B 1 21 50
    insert C 2 10 30
    insert D 2 40 45
    insert E 3 0 2)";
    execute_tree_test(tree, input, output);
    reset_string(output);
    execute_tree_test(tree, "children A", output);
    EXPECT_TRUE(output.str().find("A") == std::string::npos);
    EXPECT_TRUE(output.str().find("B") == std::string::npos);
    EXPECT_TRUE(output.str().find("C") != std::string::npos);
    EXPECT_TRUE(output.str().find("D") == std::string::npos);
    EXPECT_TRUE(output.str().find("E") == std::string::npos);
    reset_string(output);
    execute_tree_test(tree, "children B", output);
    EXPECT_TRUE(output.str().find("A") == std::string::npos);
    EXPECT_TRUE(output.str().find("B") == std::string::npos);
    EXPECT_TRUE(output.str().find("C") != std::string::npos);
    EXPECT_TRUE(output.str().find("D") != std::string::npos);
    EXPECT_TRUE(output.str().find("E") == std::string::npos);
    reset_string(output);
    execute_tree_test(tree, "children C", output);
    EXPECT_TRUE(output.str().find("A") == std::string::npos);
    EXPECT_TRUE(output.str().find("B") == std::string::npos);
    EXPECT_TRUE(output.str().find("C") == std::string::npos);
    EXPECT_TRUE(output.str().find("D") == std::string::npos);
    EXPECT_TRUE(output.str().find("E") == std::string::npos);

    execute_tree_test(tree, "insert F 2 1 9", output);
    execute_tree_test(tree, "insert G 3 4 10", output);
    execute_tree_test(tree, "children F", output);
    EXPECT_TRUE(output.str().find("A") == std::string::npos);
    EXPECT_TRUE(output.str().find("B") == std::string::npos);
    EXPECT_TRUE(output.str().find("C") == std::string::npos);
    EXPECT_TRUE(output.str().find("D") == std::string::npos);
    EXPECT_TRUE(output.str().find("E") != std::string::npos);
    EXPECT_TRUE(output.str().find("G") != std::string::npos);
    reset_string(output);
    execute_tree_test(tree, "parent F", output);
    EXPECT_TRUE(output.str().find("A") != std::string::npos);
    EXPECT_TRUE(output.str().find("B") == std::string::npos);
    EXPECT_TRUE(output.str().find("C") == std::string::npos);
    EXPECT_TRUE(output.str().find("D") == std::string::npos);
    EXPECT_TRUE(output.str().find("E") == std::string::npos);
    EXPECT_TRUE(output.str().find("F") == std::string::npos);
    EXPECT_TRUE(output.str().find("G") == std::string::npos);
}


TEST(TreeTest, TreeTest2) {
    Tree tree;
    std::ostringstream output;

    std::string input = 
    R"(
    insert A 1 1 20
    insert B 1 25 40
    insert C 1 55 70
    insert D 1 79 99
    insert E 2 2 15
    insert F 2 17 27
    insert G 2 28 39
    insert H 2 40 55
    insert I 2 60 80
    insert J 3 1 3
    insert K 3 4 10
    insert L 3 11 20
    insert M 3 22 40
    insert N 3 41 50
    insert O 3 51 70
    )";
    execute_tree_test(tree, input, output);
    reset_string(output);
    execute_tree_test(tree, "children H", output);
    EXPECT_TRUE(output.str().find("A") == std::string::npos);
    EXPECT_TRUE(output.str().find("B") == std::string::npos);
    EXPECT_TRUE(output.str().find("C") == std::string::npos);
    EXPECT_TRUE(output.str().find("D") == std::string::npos);
    EXPECT_TRUE(output.str().find("E") == std::string::npos);
    EXPECT_TRUE(output.str().find("F") == std::string::npos);
    EXPECT_TRUE(output.str().find("G") == std::string::npos);
    EXPECT_TRUE(output.str().find("H") == std::string::npos);
    EXPECT_TRUE(output.str().find("I") == std::string::npos);
    EXPECT_TRUE(output.str().find("J") == std::string::npos);
    EXPECT_TRUE(output.str().find("K") == std::string::npos);
    EXPECT_TRUE(output.str().find("L") == std::string::npos);
    EXPECT_TRUE(output.str().find("M") != std::string::npos);
    EXPECT_TRUE(output.str().find("N") != std::string::npos);
    EXPECT_TRUE(output.str().find("O") != std::string::npos);
    reset_string(output);
    execute_tree_test(tree, "parent H", output);
    EXPECT_TRUE(output.str().find("A") == std::string::npos);
    EXPECT_TRUE(output.str().find("B") != std::string::npos);
    EXPECT_TRUE(output.str().find("C") != std::string::npos);
    EXPECT_TRUE(output.str().find("D") == std::string::npos);
    EXPECT_TRUE(output.str().find("E") == std::string::npos);
    EXPECT_TRUE(output.str().find("F") == std::string::npos);
    EXPECT_TRUE(output.str().find("G") == std::string::npos);
    EXPECT_TRUE(output.str().find("H") == std::string::npos);
    EXPECT_TRUE(output.str().find("I") == std::string::npos);
    EXPECT_TRUE(output.str().find("J") == std::string::npos);
    EXPECT_TRUE(output.str().find("K") == std::string::npos);
    EXPECT_TRUE(output.str().find("L") == std::string::npos);
    EXPECT_TRUE(output.str().find("M") == std::string::npos);
    EXPECT_TRUE(output.str().find("N") == std::string::npos);
    EXPECT_TRUE(output.str().find("O") == std::string::npos);
    reset_string(output);
    execute_tree_test(tree, "remove H", output);
    execute_tree_test(tree, "searchNode H", output);
    EXPECT_TRUE(output.str().find("not found") != std::string::npos);
    reset_string(output);
    execute_tree_test(tree, "children B", output);
    EXPECT_TRUE(output.str().find("H") == std::string::npos);
    reset_string(output);
    execute_tree_test(tree, "children C", output);
    EXPECT_TRUE(output.str().find("H") == std::string::npos);
    reset_string(output);
    execute_tree_test(tree, "parent M", output);
    EXPECT_TRUE(output.str().find("H") == std::string::npos);
    reset_string(output);
    execute_tree_test(tree, "parent N", output);
    EXPECT_TRUE(output.str().find("H") == std::string::npos);
    reset_string(output);
    execute_tree_test(tree, "parent O", output);
    EXPECT_TRUE(output.str().find("H") == std::string::npos);
    reset_string(output);

}


TEST(TreeTest, TreeTest3) {
    Tree tree;
    std::ostringstream output;

    std::string input = 
    R"(
    insert A 1 1 20
    insert B 1 25 40
    insert C 1 55 70
    insert D 1 79 99
    insert E 2 2 15
    insert F 2 17 27
    insert G 2 28 39
    insert H 2 40 55
    insert I 2 60 80
    insert J 3 1 3
    insert K 3 4 10
    insert L 3 11 20
    insert M 3 22 40
    insert N 3 41 50
    insert O 3 51 70
    )";
    execute_tree_test(tree, input, output);
    reset_string(output);
    auto result = tree.search_key(38);
    std::array<std::string,3> expect = {"B","G","M"};
    int i = 0;
    while(!result.empty()){
        std::shared_ptr<TreeNode> node = result.front();
        result.pop();
        EXPECT_EQ(node->filename,expect[i]);
        i++;
    }
}


TEST(TreeTest, TreeSearchTest) {
    Tree tree;
    std::ostringstream output;

    std::string input = 
    R"(
    insert A 1 1 20
    insert B 1 25 40
    insert C 1 55 70
    insert D 1 79 99
    insert E 2 2 15
    insert F 2 17 27
    insert G 2 28 39
    insert H 2 40 55
    insert I 2 60 80
    insert J 3 1 3
    insert K 3 4 10
    insert L 3 11 20
    insert M 3 22 40
    insert N 3 41 50
    insert O 3 51 70
    )";
    execute_tree_test(tree, input, output);
    std::queue<std::shared_ptr<TreeNode>> result = tree.search_key(20);
    std::cout << "Size:" << result.size() << std::endl;

    std::array<std::string,3> expect = {"A","F","L"};
    int count = result.size();
    for(int i = 0;i < count;i++){
        std::shared_ptr<TreeNode> node = result.front();
        result.pop();
        EXPECT_EQ(node->filename,expect[i]);
    }
}