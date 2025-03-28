/**
 * @file com_thread_pool.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-03-28
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef BINDINGS_COM_IPC_COM_THREAD_POOL_H_
#define BINDINGS_COM_IPC_COM_THREAD_POOL_H_

#include <boost/asio.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/bind/bind.hpp>
#include <boost/thread.hpp>
#include <functional>  // NOLINT
#include <utility>     // NOLINT

namespace srp {
namespace bindings {
namespace com {
class ComThreadPool {
 private:
  boost::asio::thread_pool thread_pool_;

 public:
  template <typename F>
  void Post(F&& lambda) {
    boost::asio::post(thread_pool_, std::forward<F&&>(lambda));
  }
  ComThreadPool(/* args */) : thread_pool_{10} {}
  ~ComThreadPool() = default;

  static ComThreadPool& GetInstance() {
    static ComThreadPool instance{};
    return instance;
  }
};
}  // namespace com
}  // namespace bindings
}  // namespace srp

#endif  // BINDINGS_COM_IPC_COM_THREAD_POOL_H_
