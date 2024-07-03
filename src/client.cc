/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "log.h"

#include <algorithm>

#include "client.h"
#include "config.h"
#include "pikiwidb.h"
#include "slow_log.h"

namespace pikiwidb {

thread_local PClient* PClient::s_current = 0;

std::mutex monitors_mutex;
std::set<std::weak_ptr<PClient>, std::owner_less<std::weak_ptr<PClient> > > monitors;

int PClient::processInlineCmd(const char* buf, size_t bytes, std::vector<std::string>& params) {
  if (bytes < 2) {
    return 0;
  }

  std::string res;

  for (size_t i = 0; i + 1 < bytes; ++i) {
    if (buf[i] == '\r' && buf[i + 1] == '\n') {
      if (!res.empty()) {
        params.emplace_back(std::move(res));
      }

      return static_cast<int>(i + 2);
    }

    if (isblank(buf[i])) {
      if (!res.empty()) {
        params.reserve(4);
        params.emplace_back(std::move(res));
      }
    } else {
      res.reserve(16);
      res.push_back(buf[i]);
    }
  }

  return 0;
}

int PClient::handlePacket(pikiwidb::TcpConnection* obj, const char* start, int bytes) {
  s_current = this;

  const char* const end = start + bytes;
  const char* ptr = start;

  auto parseRet = parser_.ParseRequest(ptr, end);
  if (parseRet == PParseResult::error) {
    if (!parser_.IsInitialState()) {
      tcp_connection_->ActiveClose();
      return 0;
    }

    // try inline command
    std::vector<std::string> params;
    auto len = processInlineCmd(ptr, bytes, params);
    if (len == 0) {
      return 0;
    }

    ptr += len;
    parser_.SetParams(params);
    parseRet = PParseResult::ok;
  } else if (parseRet != PParseResult::ok) {
    return static_cast<int>(ptr - start);
  }

  DEFER { reset(); };

  // handle packet
  const auto& params = parser_.GetParams();
  if (params.empty()) {
    return static_cast<int>(ptr - start);
  }

  std::string cmd(params[0]);
  std::transform(params[0].begin(), params[0].end(), cmd.begin(), ::tolower);

  if (!auth_) {
    if (cmd == "auth") {
      auto now = ::time(nullptr);
      if (now <= last_auth_ + 1) {
        // avoid guess password.
        tcp_connection_->ActiveClose();
        return 0;
      } else {
        last_auth_ = now;
      }
    } else {
      ReplyError(PError_needAuth, &reply_);
      return static_cast<int>(ptr - start);
    }
  }

  DEBUG("client {}, cmd {}", tcp_connection_->GetUniqueId(), cmd);

  FeedMonitors(params);

  // check readonly slave and execute command
  handlePacketNew(obj, params, cmd);
  return static_cast<int>(ptr - start);
}

int PClient::handlePacketNew(pikiwidb::TcpConnection* obj, const std::vector<std::string>& params, const std::string& cmd) {
    ReplyError(PError_param, &reply_);
    return 0;
}

PClient* PClient::Current() { return s_current; }

PClient::PClient(TcpConnection* obj) : tcp_connection_(obj), flag_(0), name_("clientxxx") {
  auth_ = false;
  reset();
}

int PClient::HandlePackets(pikiwidb::TcpConnection* obj, const char* start, int size) {
  int total = 0;

  while (total < size) {
    auto processed = handlePacket(obj, start + total, size - total);
    if (processed <= 0) {
      break;
    }

    total += processed;
  }

  obj->SendPacket(reply_.ReadAddr(), reply_.ReadableSize());
  reply_.Clear();
  return total;
}

void PClient::OnConnect() {
    if (g_config.password.empty()) {
      SetAuth();
  }
}

void PClient::Close() {
  if (tcp_connection_) {
    tcp_connection_->ActiveClose();
  }
}

void PClient::reset() {
  s_current = nullptr;
  parser_.Reset();
}

void PClient::AddCurrentToMonitor() {
  std::unique_lock<std::mutex> guard(monitors_mutex);
  monitors.insert(std::static_pointer_cast<PClient>(s_current->shared_from_this()));
}

void PClient::FeedMonitors(const std::vector<std::string>& params) {
  assert(!params.empty());

  {
    std::unique_lock<std::mutex> guard(monitors_mutex);
    if (monitors.empty()) {
      return;
    }
  }

  char buf[512];
  int n = snprintf(buf, sizeof buf, "+[%s:%d]: \"", s_current->tcp_connection_->GetPeerIp().c_str(),
                   s_current->tcp_connection_->GetPeerPort());

  assert(n > 0);

  for (const auto& e : params) {
    if (n < static_cast<int>(sizeof buf)) {
      n += snprintf(buf + n, sizeof buf - n, "%s ", e.data());
    } else {
      break;
    }
  }

  --n;  // no space follow last param

  {
    std::unique_lock<std::mutex> guard(monitors_mutex);

    for (auto it(monitors.begin()); it != monitors.end();) {
      auto m = it->lock();
      if (m) {
        m->tcp_connection_->SendPacket(buf, n);
        m->tcp_connection_->SendPacket("\"" CRLF, 3);

        ++it;
      } else {
        monitors.erase(it++);
      }
    }
  }
}

}  // namespace pikiwidb
