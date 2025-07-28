#ifndef SIMPLE_SKIPLIST_H
#define SIMPLE_SKIPLIST_H

#include <cassert>
#include <cstdlib>
#include <memory>
#include <random>
#include <vector>

template <typename Record, typename Comparator>
class SkipList {
private:
    struct Node;

public:
    class Iterator;

    explicit SkipList(Comparator cmp = Comparator());
    ~SkipList();

    void Insert(const Record& record);
    bool Contains(const Record& record) const;

    Iterator GetIterator() const;
    size_t get_node_num() const;
    const Record* Min() const;
    const Record* Max() const;
    void dump() const;

private:
    static const int kMaxHeight = 12;
    static constexpr float kBranching = 0.25f;

    Node* head_;
    int max_height_;
    Comparator cmp_;
    mutable std::mt19937 gen_;
    mutable std::uniform_real_distribution<> dist_;
    bool userKeyEqual(const InternalKey& a, const InternalKey& b);
    int RandomHeight();
    Node* CreateNode(const Record& record, int height);
    Node* FindGreaterOrEqual(const Record& record, Node** prev = nullptr) const;
    bool Equal(const Record& a, const Record& b) const {
        return !cmp_(a, b) && !cmp_(b, a);
    }
};

// Node struct
template <typename Record, typename Comparator>
struct SkipList<Record, Comparator>::Node {
    Record record;
    std::vector<Node*> next;

    Node(const Record& r, int height) : record(r), next(height, nullptr) {}
};

// Constructor
template <typename Record, typename Comparator>
SkipList<Record, Comparator>::SkipList(Comparator cmp)
    : head_(new Node(Record(), kMaxHeight)),
      max_height_(1),
      cmp_(cmp),
      gen_(std::random_device{}()),
      dist_(0.0, 1.0) {}

// Destructor
template <typename Record, typename Comparator>
SkipList<Record, Comparator>::~SkipList() {
    Node* node = head_;
    while (node != nullptr) {
        Node* next = node->next[0];
        delete node;
        node = next;
    }
}

// Random height generator
template <typename Record, typename Comparator>
int SkipList<Record, Comparator>::RandomHeight() {
    int height = 1;
    while (height < kMaxHeight && dist_(gen_) < kBranching) {
        ++height;
    }
    return height;
}

// Create a new node
template <typename Record, typename Comparator>
typename SkipList<Record, Comparator>::Node*
SkipList<Record, Comparator>::CreateNode(const Record& r, int height) {
    return new Node(r, height);
}

// Find greater or equal node
template <typename Record, typename Comparator>
typename SkipList<Record, Comparator>::Node*
SkipList<Record, Comparator>::FindGreaterOrEqual(const Record& r, Node** prev) const {
    Node* x = head_;
    for (int level = max_height_ - 1; level >= 0; --level) {
        while (x->next[level] && cmp_(x->next[level]->record, r)) {
            x = x->next[level];
        }
        if (prev) prev[level] = x;
    }
    return x->next[0];
}

template <typename Record, typename Comparator>
bool SkipList<Record, Comparator>::userKeyEqual(const InternalKey& a, const InternalKey& b) {
    if (a.key.key_size != b.key.key_size) return false;
    return std::memcmp(a.key.key, b.key.key, a.key.key_size) == 0;
}

// Insert
template <typename Record, typename Comparator>
void SkipList<Record, Comparator>::Insert(const Record& r) {
    Node* prev[kMaxHeight];
    Node* x = FindGreaterOrEqual(r, prev);

    if (x && userKeyEqual(x->record.internal_key, r.internal_key)) {
        // 如果 user key 相同，且 r 的 sequence 較新，就更新
        if (r.internal_key.info.seq > x->record.internal_key.info.seq) {
            x->record = r;
        }
        return;  // 更新完就不需要再插入新節點
    }

    int height = RandomHeight();
    if (height > max_height_) {
        for (int i = max_height_; i < height; ++i) {
            prev[i] = head_;
        }
        max_height_ = height;
    }

    x = CreateNode(r, height);
    for (int i = 0; i < height; ++i) {
        x->next[i] = prev[i]->next[i];
        prev[i]->next[i] = x;
    }
}


// Contains
template <typename Record, typename Comparator>
bool SkipList<Record, Comparator>::Contains(const Record& r) const {
    Node* x = FindGreaterOrEqual(r);
    return x && Equal(x->record, r);
}

// Iterator class
template <typename Record, typename Comparator>
class SkipList<Record, Comparator>::Iterator {
public:
    Iterator() : head_(nullptr), current_(nullptr), cmp_() {}

    Iterator(Node* head, const Comparator& cmp)
        : head_(head), current_(head->next[0]), cmp_(cmp) {}

    bool Valid() const { return current_ != nullptr; }
    const Record& record() const { return current_->record; }

    void Next() {
        if (Valid()) current_ = current_->next[0];
    }

    void SeekToFirst() {
        current_ = head_->next[0];
    }

    void Seek(const Record& target) {
        Node* x = head_;
        for (int level = kMaxHeight - 1; level >= 0; --level) {
            while (x->next[level] && cmp_(x->next[level]->record, target)) {
                x = x->next[level];
            }
        }
        current_ = x->next[0];
    }

private:
    Node* head_;
    Node* current_;
    Comparator cmp_;
};


template <typename Record, typename Comparator>
typename SkipList<Record, Comparator>::Iterator
SkipList<Record, Comparator>::GetIterator() const {
    return Iterator(head_, cmp_);
}

template <typename Record, typename Comparator>
const Record* SkipList<Record, Comparator>::Min() const {
    Node* x = head_->next[0];
    return x ? &x->record : nullptr;
}

template <typename Record, typename Comparator>
const Record* SkipList<Record, Comparator>::Max() const {
    Node* x = head_;
    for (int level = max_height_ - 1; level >= 0; --level) {
        while (x->next[level]) {
            x = x->next[level];
        }
    }
    return x != head_ ? &x->record : nullptr;
}

template <typename Record, typename Comparator>
size_t SkipList<Record, Comparator>::get_node_num() const{
    size_t count = 0;
    Iterator it = GetIterator(); 
    it.SeekToFirst();             
    while (it.Valid()) {
        ++count;
        it.Next();
    }
    return count;
}

template <typename Record, typename Comparator>
void SkipList<Record, Comparator>::dump() const {
    std::cout << "=== SkipList Dump (Total Nodes: " << get_node_num() << ") ===" << std::endl;
    Iterator it = GetIterator();
    it.SeekToFirst();
    while (it.Valid()) {
        it.record().Dump();
        it.Next();
    }
    std::cout << "=== End of Dump ===" << std::endl;
}


#endif  // SIMPLE_SKIPLIST_H
