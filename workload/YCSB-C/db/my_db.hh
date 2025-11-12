#pragma once
#include "core/db.h"      // ycsbc::DB 接口定义
#include "core/utils.h"   // 可能用到 Status/Timer 等
#include "db_api.hh"
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
namespace ycsbc {

// 你自己的 DB “客户端句柄”，可换成你已有的 C/C++ API 封装
struct MyDbHandle {
  // 例如：文件描述符、连接对象、memtable 指针等
  // 自己的 DB 初始化时返回的上下文
  void* engine = nullptr;
};

// YCSB 适配器
class MyDB : public DB {
public:
  MyDB() = default;

  // YCSB 会在主线程先调用 Init() 一次
  void Init() override;

  int Read(const std::string &table, const std::string &key,
           const std::vector<std::string> *fields,
           std::vector<KVPair> &result) override;

  int Scan(const std::string &table, const std::string &start_key, int record_count,
           const std::vector<std::string> *fields,
           std::vector<std::vector<KVPair>> &result) override;

  int Update(const std::string &table, const std::string &key,
             std::vector<KVPair> &values) override;

  int Insert(const std::string &table, const std::string &key,
             std::vector<KVPair> &values) override;

  int Delete(const std::string &table, const std::string &key) override;

  void Close() override;


private:
  API db;
  // 每线程一个句柄：避免锁冲突
  struct ThreadLocal {
    std::unique_ptr<MyDbHandle> handle;
  };
  // 属性
  std::string db_path_;
  bool create_if_missing_ = true;
  int scan_max_items_ = 1000;

  // 线程局部存储
  static thread_local std::unique_ptr<ThreadLocal> tls_;
  // 全局资源（如引擎实例）可以放这里
  std::once_flag global_init_flag_;
  std::atomic<bool> inited_{false};
};

} // namespace ycsbc
