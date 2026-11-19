/**
 * Copyright (c) 2024
 *
 * @file fake_daemon.cc
 */
#include "ara/com/mavlink/client/testing/fake_daemon.h"

#include <thread>  // NOLINT
#include <tuple>
#include <utility>
#include <variant>

namespace ara {
namespace com {
namespace mavlink {
namespace testing {

FakeDaemon::FakeDaemon(std::string path) : path_{std::move(path)} {}

FakeDaemon::~FakeDaemon() { Stop(); }

bool FakeDaemon::Start() {
  if (running_) {
    return true;
  }
  if (!server_.Listen(path_).HasValue()) {
    return false;
  }
  running_ = true;
  acceptor_ = std::jthread([this](std::stop_token st) { AcceptLoop(st); });
  return true;
}

void FakeDaemon::Stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  acceptor_.request_stop();
  if (acceptor_.joinable()) {
    acceptor_.join();
  }
  server_.Close();
  DropConnections();
}

void FakeDaemon::DropConnections() {
  std::vector<std::unique_ptr<Conn>> old;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    old.swap(conns_);
  }
  for (auto& c : old) {
    c->sock.Shutdown();
    c->reader.request_stop();
  }
  for (auto& c : old) {
    if (c->reader.joinable()) {
      c->reader.join();
    }
  }
  connections_ = 0;
}

void FakeDaemon::AcceptLoop(std::stop_token st) {
  while (!st.stop_requested()) {
    auto acc = server_.Accept(50);
    if (!acc.HasValue() || !acc.Value().has_value()) {
      continue;
    }
    auto conn = std::make_unique<Conn>();
    conn->sock = *acc.Value();
    daemon::MessageSocket sock = conn->sock;
    conn->reader = std::jthread([this, sock](std::stop_token rst) { ReadLoop(rst, sock); });
    std::lock_guard<std::mutex> lock{mutex_};
    conns_.push_back(std::move(conn));
    connections_++;
  }
}

void FakeDaemon::ReadLoop(std::stop_token st, daemon::MessageSocket sock) {
  while (!st.stop_requested()) {
    if (!reading_.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    auto r = sock.Receive(50);
    if (r.status == daemon::RecvStatus::kClosed || r.status == daemon::RecvStatus::kProtocolError) {
      return;
    }
    if (r.status != daemon::RecvStatus::kMessage) {
      continue;
    }
    if (const auto* reg = std::get_if<daemon::RegisterInstance>(&*r.message)) {
      daemon::RegisterReply rep;
      rep.handle = reg->handle;
      rep.status = status_.load();
      rep.system_id = 1;
      rep.component_id = 1;
      if (rep.status != daemon::RegisterStatus::kOk) {
        rep.text = "rejected by the fake daemon";
      }
      {
        std::lock_guard<std::mutex> lock{mutex_};
        regs_.push_back(*reg);
      }
      std::ignore = sock.Send(rep);
      if (rep.status == daemon::RegisterStatus::kOk && links_ready_.load()) {
        for (const auto& l : reg->links) {
          daemon::LinkStatus ls;
          ls.handle = reg->handle;
          ls.link = l;
          ls.ready = true;
          std::ignore = sock.Send(ls);
        }
      }
      cv_.notify_all();
      continue;
    }
    if (std::holds_alternative<daemon::Unregister>(*r.message)) {
      unregistered_++;
      continue;
    }
    {
      std::lock_guard<std::mutex> lock{mutex_};
      inbox_.push_back(std::move(*r.message));
    }
    cv_.notify_all();
  }
}

bool FakeDaemon::SendToApp(const daemon::Message& m) {
  daemon::MessageSocket sock;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    if (conns_.empty()) {
      return false;
    }
    sock = conns_.back()->sock;
  }
  return sock.Send(m).HasValue();
}

std::optional<daemon::Message> FakeDaemon::WaitMessage(std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock{mutex_};
  if (!cv_.wait_for(lock, timeout, [this] { return !inbox_.empty(); })) {
    return std::nullopt;
  }
  auto m = std::move(inbox_.front());
  inbox_.pop_front();
  return m;
}

bool FakeDaemon::WaitRegistrations(std::size_t n, std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock{mutex_};
  return cv_.wait_for(lock, timeout, [&] { return regs_.size() >= n; });
}

std::vector<daemon::RegisterInstance> FakeDaemon::registrations() const {
  std::lock_guard<std::mutex> lock{mutex_};
  return regs_;
}

}  // namespace testing
}  // namespace mavlink
}  // namespace com
}  // namespace ara
