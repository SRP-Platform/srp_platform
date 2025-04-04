#ifndef ALIVECHECKTIMER_H
#define ALIVECHECKTIMER_H

#include <cstdlib>
#include <ctime>
#include <functional>
#include <thread>
#include <vector>

using CloseConnectionCallback = std::function<void()>;

class AliveCheckTimer {
 public:
  void setTimer(uint16_t seconds);
  void startTimer();
  void resetTimer();

  bool disabled = false;
  bool active = false;
  bool timeout = false;
  CloseConnectionCallback cb;

  ~AliveCheckTimer();

 private:
  std::vector<std::thread> timerThreads;
  void waitForResponse();
  clock_t startTime;
  uint16_t maxSeconds;
};

#endif