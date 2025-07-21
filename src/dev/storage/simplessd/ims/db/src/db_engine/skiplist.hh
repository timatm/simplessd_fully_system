#ifndef SIMPLE_SKIPLIST_H
#define SIMPLE_SKIPLIST_H

#include <atomic>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <random>
#include <vector>
#include "internal_key.hh"

template <typename Comparator = InternalKeyComparator>
class SkipList {
private:
    struct Node;

public:
    using Key = InternalKey;
    class Iterator;

    SkipList();
    ~SkipList();

    void Insert(const Key& key);
    bool Contains(const Key& key) const;

    Iterator GetIterator() const;

private:
    static const int kMaxHeight = 12;
    static constexpr float kBranching = 0.25f;

    Node* head_;
    int max_height_;
    Comparator cmp_;
    mutable std::mt19937 gen_;
    mutable std::uniform_real_distribution<> dist_;

    int RandomHeight();
    Node* CreateNode(const Key& key, int height);
    Node* FindGreaterOrEqual(const Key& key, Node** prev = nullptr) const;
    bool Equal(const Key& a, const Key& b) const { return !cmp_(a, b) && !cmp_(b, a); }
};

template <typename Comparator>
struct SkipList<Comparator>::Node {
    Key key;
    std::vector<Node*> next;

    Node(const Key& k, int height) : key(k), next(height, nullptr) {}
};

template <typename Comparator>
SkipList<Comparator>::SkipList()
    : head_(new Node(Key("", 0, ValueType::kTypeValue), kMaxHeight)),
      max_height_(1),
      gen_(std::random_device{}()),
      dist_(0.0, 1.0) {}

template <typename Comparator>
SkipList<Comparator>::~SkipList() {
    Node* node = head_;
    while (node != nullptr) {
        Node* next = node->next[0];
        delete node;
        node = next;
    }
}

template <typename Comparator>
int SkipList<Comparator>::RandomHeight() {
    int height = 1;
    while (height < kMaxHeight && dist_(gen_) < kBranching) {
        height++;
    }
    return height;
}

template <typename Comparator>
typename SkipList<Comparator>::Node*
SkipList<Comparator>::CreateNode(const Key& key, int height) {
    return new Node(key, height);
}

template <typename Comparator>
typename SkipList<Comparator>::Node*
SkipList<Comparator>::FindGreaterOrEqual(const Key& key, Node** prev) const {
    Node* x = head_;
    for (int level = max_height_ - 1; level >= 0; --level) {
        while (x->next[level] && cmp_(x->next[level]->key, key)) {
            x = x->next[level];
        }
        if (prev) prev[level] = x;
    }
    return x->next[0];
}

template <typename Comparator>
void SkipList<Comparator>::Insert(const Key& key) {
    Node* prev[kMaxHeight];
    Node* x = FindGreaterOrEqual(key, prev);

    if (x && Equal(x->key, key)) return;

    int height = RandomHeight();
    if (height > max_height_) {
        for (int i = max_height_; i < height; ++i) {
            prev[i] = head_;
        }
        max_height_ = height;
    }

    x = CreateNode(key, height);
    for (int i = 0; i < height; ++i) {
        x->next[i] = prev[i]->next[i];
        prev[i]->next[i] = x;
    }
}

template <typename Comparator>
bool SkipList<Comparator>::Contains(const Key& key) const {
    Node* x = FindGreaterOrEqual(key);
    return x && Equal(x->key, key);
}

template <typename Comparator>
class SkipList<Comparator>::Iterator {
public:
    Iterator(Node* head, const Comparator& cmp) : head_(head), current_(head->next[0]), cmp_(cmp) {}

    bool Valid() const { return current_ != nullptr; }
    const Key& key() const { return current_->key; }
    void Next() {
        if (Valid()) current_ = current_->next[0];
    }
    void SeekToFirst() {
        current_ = head_->next[0];
    }
    void Seek(const Key& target) {
        Node* x = head_;
        for (int level = kMaxHeight - 1; level >= 0; --level) {
            while (x->next[level] && cmp_(x->next[level]->key, target)) {
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

template <typename Comparator>
typename SkipList<Comparator>::Iterator SkipList<Comparator>::GetIterator() const {
    return Iterator(head_, cmp_);
}

#endif  // SIMPLE_SKIPLIST_H
