#include "EventLoop.h"
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <iostream>
#include "Util.h"
#include "base/Logging.h"

using namespace std;

__thread EventLoop* t_loopInThisThread = 0;

int createEventfd() {
  // https://www.cnblogs.com/ck1020/p/7214310.html 源码剖析
  // 这个函数就是创建一个用于事件通知的文件描述符。它类似于pipe，
  // 但是不像pipe一样需要两个描述符，它只需要一个描述就可以实现进程间通信了。
  int evtfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (evtfd < 0) {
    LOG << "Failed in eventfd";
    abort();
  }
  return evtfd;
}

EventLoop::EventLoop()
    : looping_(false),
      poller_(new Epoll()),
      wakeupFd_(createEventfd()), //可以异步唤醒
      quit_(false),
      eventHandling_(false),
      callingPendingFunctors_(false),
      threadId_(CurrentThread::tid()),
      pwakeupChannel_(new Channel(this, wakeupFd_)) {
  if (t_loopInThisThread) {
    // LOG << "Another EventLoop " << t_loopInThisThread << " exists in this
    // thread " << threadId_;
  } else {
    t_loopInThisThread = this;
    
  }
  LOG << CurrentThread::tid() << " eventloop create " << "threadId_ = " << threadId_
      << " wakeupFd_ = " << wakeupFd_;
  // pwakeupChannel_->setEvents(EPOLLIN | EPOLLET | EPOLLONESHOT);
  pwakeupChannel_->setEvents(EPOLLIN | EPOLLET);
  pwakeupChannel_->setReadHandler(bind(&EventLoop::handleRead, this));
  pwakeupChannel_->setConnHandler(bind(&EventLoop::handleConn, this));
  poller_->epoll_add(pwakeupChannel_, 0);
}

void EventLoop::handleConn() {
  // poller_->epoll_mod(wakeupFd_, pwakeupChannel_, (EPOLLIN | EPOLLET |
  // EPOLLONESHOT), 0);
  LOG << CurrentThread::tid() << " wakeupFd_ = " << wakeupFd_ << " threadId_ = " << threadId_
        << " EventLoop::handleConn";
  updatePoller(pwakeupChannel_, 0);
}

EventLoop::~EventLoop() {
  LOG << CurrentThread::tid() << " eventloop destroyed, tid=" << threadId_;
  // wakeupChannel_->disableAll();
  // wakeupChannel_->remove();
  close(wakeupFd_);
  t_loopInThisThread = NULL;
  
}

void EventLoop::wakeup() {
  uint64_t one = 1;
  ssize_t n = writen(wakeupFd_, (char*)(&one), sizeof one);
  LOG << CurrentThread::tid() << " EventLoop::wakeup(), threadId_= " << threadId_;
  if (n != sizeof one) {
    LOG << "EventLoop::wakeup() writes " << n << " bytes instead of 8";
  }
}

void EventLoop::handleRead() {
  uint64_t one = 1;
  ssize_t n = readn(wakeupFd_, &one, sizeof one);
  LOG << CurrentThread::tid() << " EventLoop::handleRead(), threadId_ = " << threadId_
      << "wakeupFd_ = " << wakeupFd_;
  if (n != sizeof one) {
    LOG << "EventLoop::handleRead() reads " << n << " bytes instead of 8";
  }
  // pwakeupChannel_->setEvents(EPOLLIN | EPOLLET | EPOLLONESHOT);
  pwakeupChannel_->setEvents(EPOLLIN | EPOLLET);
}

void EventLoop::runInLoop(Functor&& cb) {
  LOG << CurrentThread::tid() << " wakeupFd_ = " << wakeupFd_ << " threadId_ = " << threadId_
        << " EventLoop::runInLoop";
  if (isInLoopThread())
    cb();
  else
    queueInLoop(std::move(cb));
}

void EventLoop::queueInLoop(Functor&& cb) {
  LOG << CurrentThread::tid() << " wakeupFd_ = " << wakeupFd_ << " threadId_ = " << threadId_
        << " EventLoop::queueInLoop";
  {
    MutexLockGuard lock(mutex_);
    pendingFunctors_.emplace_back(std::move(cb)); //这应该就是任务队列吧
  }
  LOG << "threadId_=" << threadId_ << " current thread: " << CurrentThread::tid() << "callingPendingFunctors_=" << callingPendingFunctors_;
  if (!isInLoopThread() || callingPendingFunctors_) wakeup();
}

void EventLoop::loop() {
  LOG << CurrentThread::tid() << " wakeupFd_ = " << wakeupFd_ << " threadId_ = " << threadId_
        << " EventLoop::loop()";
  assert(!looping_);
  assert(isInLoopThread());
  looping_ = true;
  quit_ = false;
  std::vector<SP_Channel> ret;
  while (!quit_) {
    LOG << CurrentThread::tid() << " EventLoop::loop() doing, threadId_ = " << threadId_;
    // printf("EventLoop::loop() doing");
    ret.clear();
    ret = poller_->poll();
    eventHandling_ = true;
    for (auto& it : ret) it->handleEvents();
    eventHandling_ = false;
    doPendingFunctors();
    poller_->handleExpired();
  }
  looping_ = false;
}

void EventLoop::doPendingFunctors() {
  LOG << CurrentThread::tid() << " wakeupFd_ = " << wakeupFd_ << " threadId_ = " << threadId_
        << " EventLoop::doPendingFunctors()";
  std::vector<Functor> functors;
  callingPendingFunctors_ = true;

  {
    MutexLockGuard lock(mutex_);
    functors.swap(pendingFunctors_);
  }

  for (size_t i = 0; i < functors.size(); ++i) functors[i]();
  callingPendingFunctors_ = false;
}

void EventLoop::quit() {
  LOG << CurrentThread::tid() << " wakeupFd_ = " << wakeupFd_ << " threadId_ = " << threadId_
        << " EventLoop::quit()";
  quit_ = true;
  if (!isInLoopThread()) {
    wakeup();
  }
}