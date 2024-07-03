/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "io_thread_pool.h"
#include "event_loop.h"
#include "net/tcp_connection.h"

#define PIKIWIDB_VERSION "4.0.0"

class PikiwiDB final {
 public:
  PikiwiDB();
  ~PikiwiDB();

  bool ParseArgs(int ac, char* av[]);
  [[nodiscard]] const std::string& GetConfigName() const { return cfg_file_; }

  bool Init();
  void Run();
  void Stop();

  void OnNewConnection(pikiwidb::TcpConnection* obj);

 public:
  std::string cfg_file_;
  unsigned short port_;
  std::string log_level_;

 private:
  pikiwidb::IOThreadPool& io_threads_;
};

extern std::unique_ptr<PikiwiDB> g_pikiwidb;
