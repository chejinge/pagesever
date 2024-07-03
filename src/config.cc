/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "config.h"

#include <iostream>
#include <vector>

#include "config_parser.h"

namespace pikiwidb {

static void EraseQuotes(std::string& str) {
  // convert "hello" to  hello
  if (str.size() < 2) {
    return;
  }
  if (str[0] == '"' && str[str.size() - 1] == '"') {
    str.erase(str.begin());
    str.pop_back();
  }
}

extern std::vector<std::string> SplitString(const std::string& str, char seperator);

PConfig g_config;

PConfig::PConfig() {
  daemonize = false;
  pidfile = "/var/run/pikiwidb.pid";

  ip = "127.0.0.1";
  port = 9221;
  timeout = 0;

  loglevel = "notice";
  logdir = "stdout";

  databases = 16;

  // rdb
  saveseconds = 999999999;
  savechanges = 999999999;
  rdbcompression = true;
  rdbchecksum = true;
  rdbfullname = "./dump.rdb";

  maxclients = 10000;

  // slow log
  slowlogtime = 0;
  slowlogmaxlen = 128;

  hz = 10;

  includefile = "";

  maxmemory = 2 * 1024 * 1024 * 1024UL;
  maxmemorySamples = 5;
  noeviction = true;

  backend = BackEndNone;
  backendPath = "dump";
  backendHz = 10;
}

bool LoadPikiwiDBConfig(const char* cfgFile, PConfig& cfg) {
  ConfigParser parser;
  if (!parser.Load(cfgFile)) {
    return false;
  }

  if (parser.GetData<std::string>("daemonize") == "yes") {
    cfg.daemonize = true;
  } else {
    cfg.daemonize = false;
  }

  cfg.pidfile = parser.GetData<std::string>("pidfile", cfg.pidfile);

  cfg.ip = parser.GetData<std::string>("bind", cfg.ip);
  cfg.port = parser.GetData<unsigned short>("port");
  cfg.timeout = parser.GetData<int>("timeout");

  cfg.loglevel = parser.GetData<std::string>("loglevel", cfg.loglevel);
  cfg.logdir = parser.GetData<std::string>("logfile", cfg.logdir);
  EraseQuotes(cfg.logdir);
  if (cfg.logdir.empty()) {
    cfg.logdir = "stdout";
  }

  cfg.databases = parser.GetData<int>("databases", cfg.databases);
  cfg.password = parser.GetData<std::string>("requirepass");
  EraseQuotes(cfg.password);

  // alias command
  {
    std::vector<std::string> alias(SplitString(parser.GetData<std::string>("rename-command"), ' '));
    if (alias.size() % 2 == 0) {
      for (auto it(alias.begin()); it != alias.end();) {
        const std::string& oldCmd = *(it++);
        const std::string& newCmd = *(it++);
        cfg.aliases[oldCmd] = newCmd;
      }
    }
  }

  // load rdb config
  std::vector<std::string> saveInfo(SplitString(parser.GetData<std::string>("save"), ' '));
  if (!saveInfo.empty() && saveInfo.size() != 2) {
    EraseQuotes(saveInfo[0]);
    if (!(saveInfo.size() == 1 && saveInfo[0].empty())) {
      std::cerr << "bad format save rdb interval, bad string " << parser.GetData<std::string>("save") << std::endl;
      return false;
    }
  } else if (!saveInfo.empty()) {
    cfg.saveseconds = std::stoi(saveInfo[0]);
    cfg.savechanges = std::stoi(saveInfo[1]);
  }

  if (cfg.saveseconds == 0) {
    cfg.saveseconds = 999999999;
  }
  if (cfg.savechanges == 0) {
    cfg.savechanges = 999999999;
  }

  cfg.rdbcompression = (parser.GetData<std::string>("rdbcompression") == "yes");
  cfg.rdbchecksum = (parser.GetData<std::string>("rdbchecksum") == "yes");

  cfg.rdbfullname = parser.GetData<std::string>("dir", "./") + parser.GetData<std::string>("dbfilename", "dump.rdb");

  cfg.maxclients = parser.GetData<int>("maxclients", 10000);

  cfg.slowlogtime = parser.GetData<int>("slowlog-log-slower-than", 0);
  cfg.slowlogmaxlen = parser.GetData<int>("slowlog-max-len", cfg.slowlogmaxlen);

  cfg.hz = parser.GetData<int>("hz", 10);

  // load master ip port
  std::vector<std::string> master(SplitString(parser.GetData<std::string>("slaveof"), ' '));
  if (master.size() == 2) {
    cfg.masterIp = std::move(master[0]);
    cfg.masterPort = static_cast<unsigned short>(std::stoi(master[1]));
  }
  cfg.masterauth = parser.GetData<std::string>("masterauth");

  // load modules' names
  cfg.modules = parser.GetDataVector("loadmodule");

  cfg.includefile = parser.GetData<std::string>("include");  // TODO multi files include

  // lru cache
  cfg.maxmemory = parser.GetData<uint64_t>("maxmemory", 2 * 1024 * 1024 * 1024UL);
  cfg.maxmemorySamples = parser.GetData<int>("maxmemory-samples", 5);
  cfg.noeviction = (parser.GetData<std::string>("maxmemory-policy", "noeviction") == "noeviction");

  // io threads
  cfg.io_threads_num = parser.GetData<int>("io-threads", 1);

  // backend
  cfg.backend = parser.GetData<int>("backend", BackEndNone);
  cfg.backendPath = parser.GetData<std::string>("backendpath", cfg.backendPath);
  EraseQuotes(cfg.backendPath);
  cfg.backendHz = parser.GetData<int>("backendhz", 10);

  return cfg.CheckArgs();
}

bool PConfig::CheckArgs() const {
#define RETURN_IF_FAIL(cond)        \
  if (!(cond)) {                    \
    std::cerr << #cond " failed\n"; \
    return false;                   \
  }

  RETURN_IF_FAIL(port > 0);
  RETURN_IF_FAIL(databases > 0);
  RETURN_IF_FAIL(maxclients > 0);
  RETURN_IF_FAIL(hz > 0 && hz < 500);
  RETURN_IF_FAIL(maxmemory >= 512 * 1024 * 1024UL);
  RETURN_IF_FAIL(maxmemorySamples > 0 && maxmemorySamples < 10);
  RETURN_IF_FAIL(io_threads_num > 0 && io_threads_num < 129); // as redis
  RETURN_IF_FAIL(backend >= BackEndNone && backend < BackEndMax);
  RETURN_IF_FAIL(backendHz >= 1 && backendHz <= 50);

#undef RETURN_IF_FAIL

  return true;
}

bool PConfig::CheckPassword(const std::string& pwd) const { return password.empty() || password == pwd; }

}  // namespace pikiwidb
