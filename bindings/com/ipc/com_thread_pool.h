/**
 * @file com_thread_pool.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief Standard C++ implementation of a thread pool (no Boost dependency)
 * @version 0.2
 * @date 2026-03-26
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef BINDINGS_COM_IPC_COM_THREAD_POOL_H_
#define BINDINGS_COM_IPC_COM_THREAD_POOL_H_

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace srp {
namespace bindings {
namespace com {

class ComThreadPool {
 private:
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;

  std::mutex queue_mutex_;
  std::condition_variable condition_;
  bool stop_{false};

  // Prywatny konstruktor dla Singletona
  explicit ComThreadPool(size_t threads = 10) {
    for (size_t i = 0; i < threads; ++i) {
      workers_.emplace_back([this] {
        while (true) {
          std::function<void()> task;
          {
            std::unique_lock<std::mutex> lock(this->queue_mutex_);
            this->condition_.wait(
                lock, [this] { return this->stop_ || !this->tasks_.empty(); });

            if (this->stop_ && this->tasks_.empty()) {
              return;
            }

            task = std::move(this->tasks_.front());
            this->tasks_.pop();
          }
          task();
        }
      });
    }
  }

 public:
  // Usunięcie konstruktorów kopiujących
  ComThreadPool(const ComThreadPool&) = delete;
  ComThreadPool& operator=(const ComThreadPool&) = delete;

  ~ComThreadPool() {
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      stop_ = true;
    }
    condition_.notify_all();
    for (std::thread& worker : workers_) {
      if (worker.joinable()) {
        worker.join();
      }
    }
  }

  template <typename F>
  void Post(F&& lambda) {
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      if (stop_) return;
      tasks_.emplace(std::forward<F>(lambda));
    }
    condition_.notify_one();
  }

  static ComThreadPool& GetInstance() {
    static ComThreadPool instance{10};
    return instance;
  }
};

}  // namespace com
}  // namespace bindings
}  // namespace srp

#endif  // BINDINGS_COM_IPC_COM_THREAD_POOL_H_