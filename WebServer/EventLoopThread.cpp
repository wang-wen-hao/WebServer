#include "EventLoopThread.h"
#include <functional>

EventLoopThread::EventLoopThread()
    : loop_(NULL),
      exiting_(false),
      thread_(bind(&EventLoopThread::threadFunc, this), "EventLoopThread"),
      mutex_(),
      cond_(mutex_) {
        LOG << CurrentThread::tid() << " EventLoopThread create";
      }

EventLoopThread::~EventLoopThread() {
  LOG << CurrentThread::tid() << " EventLoopThread::~EventLoopThread()";
  exiting_ = true;
  if (loop_ != NULL) {
    loop_->quit();
    thread_.join();
  }
}

EventLoop* EventLoopThread::startLoop() {
  assert(!thread_.started());
  LOG << CurrentThread::tid() << " EventLoopThread::startLoop()";
  thread_.start();
  {
    MutexLockGuard lock(mutex_);
    // 一直等到threadFun在Thread里真正跑起来
    while (loop_ == NULL) cond_.wait();
  }
  LOG << CurrentThread::tid() << " loop_'s tid = " << loop_->getLoopTid(); // 这里CurrentThread::tid()是主线程, 而loop_的tid是子线程的
  return loop_;
}

void EventLoopThread::threadFunc() {
  LOG << CurrentThread::tid() << " EventLoopThread::threadFunc()";
  EventLoop loop;
  LOG << "loop's tid = " << loop.getLoopTid() << " current tid = " << CurrentThread::tid();
  {
    MutexLockGuard lock(mutex_);
    loop_ = &loop;
    cond_.notify();
  }

  loop.loop();
  // assert(exiting_);
  loop_ = NULL;
}