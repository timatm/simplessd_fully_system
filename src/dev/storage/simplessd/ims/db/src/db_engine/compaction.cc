#include "compaction.hh"
#include <algorithm>
#include <limits>
#include <cstring>


CompactionRunner::CompactionRunner(API *db,const InternalKeyComparator* icmp,CompactionPlan config)
    : smgr_(db->getSSTable()), lmgr_(db->getLogManager()), tree_(db->getLSMTree()), icmp_(icmp),
    nums_(0), packType_(db->getPackType()), config_(config){

    Options option;
    
    option.lower = config_.lower;
    option.upper = config_.upper;
    if(config.src_level == 0){
        srcLevelIter_ = std::make_unique<Level0Iterator>(smgr_,lmgr_,icmp_,tree_,option);
    }
    if(config.src_level > 0 && config.src_level < MAX_LEVEL){
        srcLevelIter_ = std::make_unique<LevelNIterator>(smgr_,lmgr_,icmp_,tree_,config_.src_level,option);
    }
    dstLevelIter_ = std::make_unique<LevelNIterator>(smgr_,lmgr_,icmp_,tree_,config_.dst_level,option);
    while(!sortedList_.empty()) sortedList_.pop();
    hash_num_.clear();
    if (packType_ == PackingType::kHash) hash_num_.assign(SLOT_NUM, 0);
}

CompactionRunner::CompactionRunner(SstableManager *smgr,LogManager *lmgr,LSMTree* tree,const InternalKeyComparator* icmp,PackingType type,CompactionPlan config)
    : smgr_(smgr), lmgr_(lmgr), tree_(tree), icmp_(icmp),
    nums_(0), packType_(type), config_(config){
    Options option;
    
    option.lower = config_.lower;
    option.upper = config_.upper;
    if(config.src_level == 0){
        srcLevelIter_ = std::make_unique<Level0Iterator>(smgr_,lmgr_,icmp_,tree_,option);
    }
    if(config.src_level > 0 && config.src_level < MAX_LEVEL){
        srcLevelIter_ = std::make_unique<LevelNIterator>(smgr_,lmgr_,icmp_,tree_,config_.src_level,option);
    }
    dstLevelIter_ = std::make_unique<LevelNIterator>(smgr_,lmgr_,icmp_,tree_,config_.dst_level,option);
    while(!sortedList_.empty()) sortedList_.pop();
    hash_num_.clear();
    if (packType_ == PackingType::kHash) hash_num_.assign(SLOT_NUM, 0);
}

bool CompactionRunner::same_user_key(std::string_view a, std::string_view b) {
    InternalKey ia{}, ib{};
    std::memcpy(&ia, a.data(), sizeof(InternalKey));
    std::memcpy(&ib, b.data(), sizeof(InternalKey));
    if (ia.key.key_size != ib.key.key_size) return false;
    return std::memcmp(ia.key.key, ib.key.key, ia.key.key_size) == 0;
}

uint8_t CompactionRunner::value_type_of(std::string_view ikey) {
    InternalKey ik{}; 
    std::memcpy(&ik, ikey.data(), sizeof(InternalKey));
    return static_cast<uint8_t>(ik.info.type);
}

bool CompactionRunner::memTableIsFull() {
    switch (packType_) {
        case PackingType::kKeyPerPage:
            return nums_ >= IMS_PAGE_NUM;
        case PackingType::kHash:
            return std::any_of(hash_num_.begin(), hash_num_.end(),
                               [](uint32_t count) { return count >= IMS_PAGE_NUM; });
        case PackingType::kKeyRange:
            return nums_ >= SLOT_NUM * IMS_PAGE_NUM;
        default:
            return false;
    }
}

Status CompactionRunner::Run() {
    if (!srcLevelIter_ || !dstLevelIter_) return Status::IOError("iterators not ready");
    auto s = srcLevelIter_->Init();
    if (!s.ok()) return s;
    auto t = dstLevelIter_->Init();
    if (!t.ok()) return t;
    
    auto less_ikey = [&](std::string_view a, std::string_view b)-> bool {
        InternalKey ia{}, ib{};
        std::memcpy(&ia, a.data(), sizeof(InternalKey));
        std::memcpy(&ib, b.data(), sizeof(InternalKey));
        return (*icmp_)(ia, ib); // a < b ?
    };

    auto emit = [&](std::string_view k) -> Status {
        sortedList_.emplace(k.data(), k.size());
        if(packType_ == PackingType::kHash){
            InternalKey key{};
            std::memcpy(&key, k.data(), sizeof(InternalKey));
            auto idx = HashModN(key, SLOT_NUM);
            if (idx >= hash_num_.size()) return Status::IOError("hash_num_ not initialized");
            hash_num_[idx]++;
        }
        else{
            ++nums_;
        }
        if (memTableIsFull()) {
            if (sortedList_.empty()) return Status::IOError("flush with empty queue");
            InternalKey min = InternalKey::Decode(sortedList_.front());
            InternalKey max = InternalKey::Decode(sortedList_.back());
            std::string buf = smgr_->packingTable(sortedList_);
            smgr_->writeSSTable(config_.dst_level,min,max,buf);
            while (!sortedList_.empty()) sortedList_.pop();
            nums_ = 0;
            if (packType_ == PackingType::kHash) std::fill(hash_num_.begin(), hash_num_.end(), 0);
        }
        return Status::OK();
    };

    bool l_valid = srcLevelIter_->Valid();
    bool r_valid = dstLevelIter_->Valid();

    std::string last_user_key;  // 版本折疊用（internal key 排序已保證最新版本先出）
    bool have_last = false;

    while (l_valid || r_valid) {
        bool take_left = false;
        if (!r_valid) take_left = true;
        else if (!l_valid) take_left = false;
        else take_left = less_ikey(srcLevelIter_->key(), dstLevelIter_->key());

        std::string_view cur_key;

        if (take_left) {
            cur_key = srcLevelIter_->key();
            srcLevelIter_->Next();
            l_valid = srcLevelIter_->Valid();
        } else {
            cur_key = dstLevelIter_->key();
            dstLevelIter_->Next();
            r_valid = dstLevelIter_->Valid();
        }

        // 同 user key 的舊版本折疊（只保留第一個 = 最新）
        if (!have_last || !same_user_key(cur_key, std::string_view(last_user_key))) {
            last_user_key.assign(cur_key.data(), cur_key.size());
            have_last = true;

            // 你可選擇是否保留 deletion；此處先全部保留：
            // if (value_type_of(cur_key) == (uint8_t)ValueType::kTypeDeletion) { /* 視需求處理 */ }

            auto s = emit(cur_key);
            if (!s.ok()) return s;
        } else {
            continue;
        }
    }
    if (!sortedList_.empty()) {
        InternalKey min = InternalKey::Decode(sortedList_.front());
        InternalKey max = InternalKey::Decode(sortedList_.back());
        std::string buf = smgr_->packingTable(sortedList_);
        smgr_->writeSSTable(config_.dst_level,min,max,buf);
        while (!sortedList_.empty()) sortedList_.pop();
        nums_ = 0;
    }
    smgr_->waitAllTasksDone();
    return Status::OK();
}