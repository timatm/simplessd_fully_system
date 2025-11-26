#include "db/my_db.hh"

#include "core/properties.h"  // YCSB 属性系统
#include <cassert>
#include <iostream>
#include <memory>
using namespace std;
namespace ycsbc {

thread_local std::unique_ptr<MyDB::ThreadLocal> MyDB::tls_ = nullptr;

void MyDB::Init() {
  // 全局初始化（只做一次）
  std::call_once(global_init_flag_, [&]() {
    db.open();
    // inited_.store(true, std::memory_order_release);
  });
}

void MyDB::Close(){
  db.close();
  return;
}


int MyDB::Read(const string & /*table*/, const string &key,
               const vector<string> * /*fields*/,
               vector<KVPair> &result) {
  // assert(tls_ && "InitThread not called?");
  string value;
  Status s = db.get(key,value);

  // if (!s.ok()) return kNotFound;
  result.clear();
  result.emplace_back("field0", value); // YCSB 需要 KVPair 列表
  return kOK;
}

int MyDB::Insert(const string & /*table*/, const string &key, vector<KVPair> &values) {
  string value;
  if (!values.empty()) value = values[0].second;
  // std::cout << "DB engine put info:" << std::endl;
  // std::cout << "KEY  :" << key << std::endl;
  // std::cout << "VALUE:" << value << std::endl;
  Status s = db.put(key,value);
  return s.ok() ? kOK : kError;
}

int MyDB::Update(const string & /*table*/, const string &key, vector<KVPair> &values) {
  string value;
  if (!values.empty()) value = values[0].second;
  // std::cout << "DB engine update info:" << std::endl;
  // std::cout << "KEY  :" << key << std::endl;
  // std::cout << "VALUE:" << value << std::endl;
  Status s = db.put(key,value);
  return s.ok() ? kOK : kError;
}

int MyDB::Delete(const string & /*table*/, const string &key) {
  std::cout << "DB engine delete info:" << std::endl;
  std::cout << "KEY  :" << key << std::endl;
  string value;
  Status s = db.delete_key(key,value);
  return s.ok() ? kOK : kError;
}

int MyDB::Scan(const string & /*table*/, const string &start_key, int record_count,
               const vector<string> * /*fields*/,
               vector<vector<KVPair>> &result) {
  // assert(tls_ && "InitThread not called?");
  // TODO: 实现从 start_key 开始的正向迭代，最多取 record_count 条
  // 伪代码：
  // auto it = engine_seek(tls_->handle.get(), start_key);
  // for (int i = 0; i < record_count && it.valid(); ++i, it.next()) { ... }
  result.clear();
  for (int i = 0; i < record_count; ++i) {
    vector<KVPair> row;
    row.emplace_back("field0", "mock_scan_value");
    result.emplace_back(std::move(row));
  }
  return kOK;
}


} // namespace ycsbc
