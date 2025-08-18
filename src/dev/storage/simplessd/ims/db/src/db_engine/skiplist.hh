#ifndef SIMPLE_SKIPLIST_H
#define SIMPLE_SKIPLIST_H

#include <cassert>
#include <cstdlib>
#include <memory>
#include <random>
#include <vector>
#include <iostream>   // for std::cout
#include <cstring>    // for std::memcmp

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

    // 若你就是只服務擁有 InternalKey 的 Record，這個保留即可
    bool userKeyEqual(const InternalKey& a, const InternalKey& b);

    int RandomHeight();
    Node* CreateNode(const Record& record, int height);

    // 尋找 >= r 的第一個節點；prev（如提供）記錄各層最後一個 < r 的節點
    Node* FindGreaterOrEqual(const Record& r, Node** prev = nullptr) const;

    // 修正為以 Record 為比較基準的「嚴格小於 r 的最後一個節點」
    Node* FindLessThan(const Record& r) const;

    // 最後一個節點
    Node* FindLast() const;

    bool Equal(const Record& a, const Record& b) const {
        return !cmp_(a, b) && !cmp_(b, a);
    }
};

// ---------------- Node ----------------
template <typename Record, typename Comparator>
struct SkipList<Record, Comparator>::Node {
    Record record;
    std::vector<Node*> next;

    Node(const Record& r, int height) : record(r), next(height, nullptr) {}
};

// ---------------- Ctor/Dtor ----------------
template <typename Record, typename Comparator>
SkipList<Record, Comparator>::SkipList(Comparator cmp)
    : head_(new Node(Record(), kMaxHeight)),
      max_height_(1),
      cmp_(cmp),
      gen_(std::random_device{}()),
      dist_(0.0, 1.0) {}

template <typename Record, typename Comparator>
SkipList<Record, Comparator>::~SkipList() {
    Node* node = head_;
    while (node != nullptr) {
        Node* next = node->next[0];
        delete node;
        node = next;
    }
}

// ---------------- RandomHeight/CreateNode ----------------
template <typename Record, typename Comparator>
int SkipList<Record, Comparator>::RandomHeight() {
    int height = 1;
    while (height < kMaxHeight && dist_(gen_) < kBranching) {
        ++height;
    }
    return height;
}

template <typename Record, typename Comparator>
typename SkipList<Record, Comparator>::Node*
SkipList<Record, Comparator>::CreateNode(const Record& r, int height) {
    return new Node(r, height);
}

// ---------------- Equal user key (專用於你的 Record/InternalKey) ----------------
template <typename Record, typename Comparator>
bool SkipList<Record, Comparator>::userKeyEqual(const InternalKey& a, const InternalKey& b) {
    if (a.key.key_size != b.key.key_size) return false;
    return std::memcmp(a.key.key, b.key.key, a.key.key_size) == 0;
}

// ---------------- FindGreaterOrEqual ----------------
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

// ---------------- Insert ----------------
template <typename Record, typename Comparator>
void SkipList<Record, Comparator>::Insert(const Record& r) {
    Node* prev[kMaxHeight];
    Node* x = FindGreaterOrEqual(r, prev);

    // 你的需求：同 user key 取更大 seq 的覆蓋
    if (x && userKeyEqual(x->record.internal_key, r.internal_key)) {
        if (r.internal_key.info.seq > x->record.internal_key.info.seq) {
            x->record = r;
        }
        return;
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

// ---------------- Contains ----------------
template <typename Record, typename Comparator>
bool SkipList<Record, Comparator>::Contains(const Record& r) const {
    Node* x = FindGreaterOrEqual(r);
    return x && Equal(x->record, r);
}

// ---------------- Iterator ----------------
template <typename Record, typename Comparator>
class SkipList<Record, Comparator>::Iterator {
public:
    Iterator() : list_(nullptr), head_(nullptr), current_(nullptr), cmp_() {}

    // 改成帶 Self 指標，便於呼叫 FindLessThan/FindLast
    explicit Iterator(const SkipList* list)
        : list_(list),
          head_(list->head_),
          current_(list->head_->next[0]),
          cmp_(list->cmp_) {}

    bool Valid() const { return current_ != nullptr; }
    const Record& record() const { return current_->record; }

    void Next() {
        if (Valid()) current_ = current_->next[0];
    }

    void Prev() {
        if (!list_) return;
        if (!Valid()) { 
            current_ = list_->FindLast();
            return;
        }
        current_ = list_->FindLessThan(current_->record);
    }

    void SeekToFirst() { current_ = head_->next[0]; }

    void SeekToLast() {
        if (!list_) return;
        current_ = list_->FindLast();
    }

    void Seek(const Record& target) {
        if (!list_) return;
        current_ = list_->FindGreaterOrEqual(target, nullptr);
    }

private:
    const SkipList* list_;  // <== 新增：指向外部 SkipList，方便用其輔助函式
    Node* head_;
    Node* current_;
    Comparator cmp_;
};

// ---------------- GetIterator ----------------
template <typename Record, typename Comparator>
typename SkipList<Record, Comparator>::Iterator
SkipList<Record, Comparator>::GetIterator() const {
    return Iterator(this);
}

// ---------------- Min/Max ----------------
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

// ---------------- get_node_num/dump ----------------
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
    std::cout << "=== SkipList Dump (Total Nodes: " << get_node_num() << ") ===\n";
    Iterator it = GetIterator();
    it.SeekToFirst();
    while (it.Valid()) {
        it.record().Dump();  // 需確保 Record 有 Dump()
        it.Next();
    }
    std::cout << "=== End of Dump ===\n";
}

// ---------------- FindLessThan/FindLast（修正版） ----------------
template <typename Record, typename Comparator>
typename SkipList<Record, Comparator>::Node*
SkipList<Record, Comparator>::FindLessThan(const Record& r) const {
    Node* x = head_;
    for (int level = max_height_ - 1; level >= 0; --level) {
        while (x->next[level] && cmp_(x->next[level]->record, r)) {
            x = x->next[level];
        }
    }
    return (x == head_) ? nullptr : x;
}

template <typename Record, typename Comparator>
typename SkipList<Record, Comparator>::Node*
SkipList<Record, Comparator>::FindLast() const {
    Node* x = head_;
    for (int level = max_height_ - 1; level >= 0; --level) {
        while (x->next[level]) {
            x = x->next[level];
        }
    }
    return (x == head_) ? nullptr : x;
}

#endif  // SIMPLE_SKIPLIST_H
