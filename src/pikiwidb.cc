/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

//
//  PikiwiDB.cc
#include "pikiwidb.h"

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#include <iostream>
#include <thread>
#include <memory>

#include "std/log.h"

#include "client.h"

#include "config.h"
#include "db.h"
#include "slow_log.h"

#include "pikiwidb_logo.h"

std::unique_ptr<PikiwiDB> g_pikiwidb;

PikiwiDB::PikiwiDB() : io_threads_(pikiwidb::IOThreadPool::Instance()), port_(0) { }

PikiwiDB::~PikiwiDB() = default;

static void Usage() {
  std::cerr << "Usage:  ./pikiwidb-server [/path/to/redis.conf] [options]\n\
        ./pikiwidb-server -v or --version\n\
        ./pikiwidb-server -h or --help\n\
Examples:\n\
        ./pikiwidb-server (run the server with default conf)\n\
        ./pikiwidb-server /etc/redis/6379.conf\n\
        ./pikiwidb-server --port 7777\n\
        ./pikiwidb-server --port 7777 --slaveof 127.0.0.1 8888\n\
        ./pikiwidb-server /etc/myredis.conf --loglevel verbose\n";
}

bool PikiwiDB::ParseArgs(int ac, char* av[]) {
  for (int i = 0; i < ac; i++) {
    if (cfg_file_.empty() && ::access(av[i], R_OK) == 0) {
      cfg_file_ = av[i];
      continue;
    } else if (strncasecmp(av[i], "-v", 2) == 0 || strncasecmp(av[i], "--version", 9) == 0) {
      std::cerr << "PikiwiDB Server v=" << PIKIWIDB_VERSION << " bits=" << (sizeof(void*) == 8 ? 64 : 32) << std::endl;

      exit(0);
      return true;
    } else if (strncasecmp(av[i], "-h", 2) == 0 || strncasecmp(av[i], "--help", 6) == 0) {
      Usage();
      exit(0);
      return true;
    } else if (strncasecmp(av[i], "--port", 6) == 0) {
      if (++i == ac) {
        return false;
      }
      port_ = static_cast<unsigned short>(std::atoi(av[i]));
    } else if (strncasecmp(av[i], "--loglevel", 10) == 0) {
      if (++i == ac) {
        return false;
      }
      log_level_ = std::string(av[i]);
    } else if (strncasecmp(av[i], "--slaveof", 9) == 0) {
      if (i + 2 >= ac) {
        return false;
      }
    } else {
      std::cerr << "Unknow option " << av[i] << std::endl;
      return false;
    }
  }

  return true;
}



void PikiwiDB::OnNewConnection(pikiwidb::TcpConnection* obj) {
  INFO("New connection from {}:{}", obj->GetPeerIp(), obj->GetPeerPort());

  auto client = std::make_shared<pikiwidb::PClient>(obj);
  obj->SetContext(client);

  client->OnConnect();

  auto msg_cb = std::bind(&pikiwidb::PClient::HandlePackets, client.get(),
                          std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
  obj->SetMessageCallback(msg_cb);
  obj->SetOnDisconnect([](pikiwidb::TcpConnection* obj) { INFO("disconnect from {}", obj->GetPeerIp()); });
  obj->SetNodelay(true);
  obj->SetEventLoopSelector([]() { return pikiwidb::IOThreadPool::Instance().Next(); });
}

bool PikiwiDB::Init() {
  using namespace pikiwidb;
  if (port_ != 0) {
    g_config.port = port_;
  }

  if (!log_level_.empty()) {
    g_config.loglevel = log_level_;
  }

  NewTcpConnectionCallback cb = std::bind(&PikiwiDB::OnNewConnection, this, std::placeholders::_1);
  if (!io_threads_.Init(g_config.ip.c_str(), g_config.port, cb)) {
    return false;
  }
  io_threads_.SetWorkerNum((size_t)(g_config.io_threads_num));

  PSlowLog::Instance().SetThreshold(g_config.slowlogtime);
  PSlowLog::Instance().SetLogLimit(static_cast<std::size_t>(g_config.slowlogmaxlen));

  // output logo to console
  char logo[512] = "";
  snprintf(logo, sizeof logo - 1, pikiwidbLogo, PIKIWIDB_VERSION, static_cast<int>(sizeof(void*)) * 8,
           static_cast<int>(g_config.port));
  std::cout << logo;

  return true;
}
void PikiwiDB::Run() {
  io_threads_.SetName("pikiwi-main");
  io_threads_.Run(0, nullptr);
  INFO("server exit running");
}

void PikiwiDB::Stop() { io_threads_.Exit(); }

static void InitLogs() {
  logger::Init("logs/pikiwidb_server.log");

#if BUILD_DEBUG
  spdlog::set_level(spdlog::level::debug);
#else
  spdlog::set_level(spdlog::level::info);
#endif
}

int main(int ac, char* av[]) {
  g_pikiwidb = std::make_unique<PikiwiDB>();

  InitLogs();
  INFO("pikiwidb server start...");

  if (!g_pikiwidb->ParseArgs(ac - 1, av + 1)) {
    Usage();
    return -1;
  }

  if (!g_pikiwidb->GetConfigName().empty()) {
    if (!LoadPikiwiDBConfig(g_pikiwidb->GetConfigName().c_str(), pikiwidb::g_config)) {
      std::cerr << "Load config file [" << g_pikiwidb->GetConfigName() << "] failed!\n";
      return -1;
    }
  }

  if (pikiwidb::g_config.daemonize) {
    pid_t pid;
    ::posix_spawn(&pid, av[0], nullptr, nullptr, av, nullptr);
  }

  if (g_pikiwidb->Init()) {
    g_pikiwidb->Run();
  }

  return 0;
}
