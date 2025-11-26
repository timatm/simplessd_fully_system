//
//  basic_db.cc
//  YCSB-C
//
//  Created by Jinglei Ren on 12/17/14.
//  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
//

#include "db/db_factory.h"


#ifndef ENABLE_REDIS
#define ENABLE_REDIS 0
#endif

#ifndef ENABLE_TBB
#define ENABLE_TBB 0
#endif

#include <string>
#include "db/basic_db.h"
#include "db/lock_stl_db.h"

#if ENABLE_TBB
#include "db/tbb_rand_db.h"
#include "db/tbb_scan_db.h"
#endif

#if ENABLE_REDIS
#include "redis_db.h"
#endif
#include "my_db.hh"

using namespace std;
using ycsbc::DB;
using ycsbc::DBFactory;

DB* DBFactory::CreateDB(utils::Properties &props) {
  if (props["dbname"] == "basic") {
    return new BasicDB;
  } else if (props["dbname"] == "lock_stl") {
    return new LockStlDB;
  } else if (props["dbname"] == "mydb") {
    return new MyDB;
#if ENABLE_REDIS
  } else if (props["dbname"] == "redis") {
    int port = stoi(props["port"]);
    int slaves = stoi(props["slaves"]);
    return new RedisDB(props["host"].c_str(), port, slaves);
#endif
#if ENABLE_TBB
  } else if (props["dbname"] == "tbb_rand") {
    return new TbbRandDB;
  } else if (props["dbname"] == "tbb_scan") {
    return new TbbScanDB;
#endif
  } else return NULL;
}

// ./ycsbc -db lock_stl -threads 4 -P workloads/workloada.spec
// ./ycsbc -db lock_stl -threads 1 -P workloads/workloada.spec
// ./ycsbc -db mydb -threads 1 -P workloads/workloada.spec