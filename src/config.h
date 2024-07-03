/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <map>
#include <string>


namespace pikiwidb {

enum BackEndType {
  BackEndNone = 0,
  BackEndLeveldb = 1,
  BackEndMax = 2,
};

struct PConfig {
  bool daemonize;
  std::string pidfile;

  std::string ip;
  unsigned short port;

  int timeout;

  std::string loglevel;
  std::string logdir;  // the log directory, differ from redis

  int databases;

  // auth
  std::string password;

  std::map<std::string, std::string> aliases;

  // @ rdb
  // save seconds changes
  int saveseconds;
  int savechanges;
  bool rdbcompression;  // yes
  bool rdbchecksum;     // yes
  std::string rdbfullname;  // ./dump.rdb

  int maxclients;  // 10000

  int slowlogtime;    // 1000 microseconds
  int slowlogmaxlen;  // 128

  int hz;  // 10  [1,500]

  std::string masterIp;
  unsigned short masterPort;  // replication
  std::string masterauth;

  std::string runid;

  std::string includefile;  // the template config

  std::vector<std::string> modules;  // modules

  // use redis as cache, level db as backup
  uint64_t maxmemory;    // default 2GB
  int maxmemorySamples;  // default 5
  bool noeviction;       // default true

  // THREADED I/O
  int io_threads_num;

  int backend;  // enum BackEndType
  std::string backendPath;
  int backendHz;  // the frequency of dump to backend

  PConfig();

  [[nodiscard]] bool CheckArgs() const;
  [[nodiscard]] bool CheckPassword(const std::string& pwd) const;
};

extern PConfig g_config;

extern bool LoadPikiwiDBConfig(const char* cfgFile, PConfig& cfg);

}  // namespace pikiwidb

