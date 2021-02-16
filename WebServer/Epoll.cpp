#include "Epoll.h"
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <deque>
#include <queue>
#include "Util.h"
#include "base/Logging.h"
#include "base/CurrentThread.h"

#include <arpa/inet.h>
#include <iostream>
using namespace std;

const int EVENTSNUM = 4096;
const int EPOLLWAIT_TIME = 10000; //等待１０秒

typedef shared_ptr<Channel> SP_Channel;

Epoll::Epoll() : epollFd_(epoll_create1(EPOLL_CLOEXEC)), events_(EVENTSNUM) {
  assert(epollFd_ > 0);
  LOG << CurrentThread::tid() << " Epoll create, epollFd_ = " << epollFd_;
}
Epoll::~Epoll() {
  LOG << CurrentThread::tid() << " Epoll destroyed, epollFd_ = " << epollFd_;
}

// 注册新描述符
void Epoll::epoll_add(SP_Channel request, int timeout) {
  LOG << CurrentThread::tid() << " Epoll::epoll_add, epollFd_ = " << epollFd_
      << " fd = " << request->getFd();
  int fd = request->getFd();
  if (timeout > 0) {
    add_timer(request, timeout);
    fd2http_[fd] = request->getHolder();
  }
  struct epoll_event event;
  event.data.fd = fd;
  event.events = request->getEvents();

  request->EqualAndUpdateLastEvents(); //这干嘛用的？

  fd2chan_[fd] = request;
  if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &event) < 0) {
    perror("epoll_add error");
    fd2chan_[fd].reset();
  }
}

// 修改描述符状态
void Epoll::epoll_mod(SP_Channel request, int timeout) {
  LOG << CurrentThread::tid() << " Epoll::epoll_mod, epollFd_ = " << epollFd_
      << " fd = " << request->getFd();
  if (timeout > 0) add_timer(request, timeout);
  int fd = request->getFd();
  // 如果上一个事件不等于当前的事件
  if (!request->EqualAndUpdateLastEvents()) {
    struct epoll_event event;
    event.data.fd = fd;
    event.events = request->getEvents();
    if (epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &event) < 0) {
      perror("epoll_mod error");
      fd2chan_[fd].reset();
    }
  }
}

// 从epoll中删除描述符
void Epoll::epoll_del(SP_Channel request) {
  LOG << CurrentThread::tid() << " Epoll::epoll_del, epollFd_ = " << epollFd_
      << " fd = " << request->getFd();
  int fd = request->getFd();
  struct epoll_event event;
  event.data.fd = fd;
  event.events = request->getLastEvents();
  // event.events = 0;
  // request->EqualAndUpdateLastEvents()
  if (epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, &event) < 0) {
    perror("epoll_del error");
  }
  fd2chan_[fd].reset();
  fd2http_[fd].reset();
}

// 返回活跃事件数
std::vector<SP_Channel> Epoll::poll() {
  LOG << CurrentThread::tid() << " Epoll::poll(), epollFd_ = " << epollFd_;
  //死循环
  while (true) {
    int event_count =
        epoll_wait(epollFd_, &*events_.begin(), events_.size(), EPOLLWAIT_TIME); //这里的等待事件是10秒,10秒之后会触发
    if (event_count < 0) perror("epoll wait error");
    std::vector<SP_Channel> req_data = getEventsRequest(event_count);
    if (req_data.size() > 0) return req_data;
  }
}

void Epoll::handleExpired() {
  LOG << CurrentThread::tid() << " Epoll::handleExpired(), epollFd_ = " << epollFd_;
  timerManager_.handleExpiredEvent(); 
}

// 分发处理函数
std::vector<SP_Channel> Epoll::getEventsRequest(int events_num) {
  LOG << CurrentThread::tid() << " Epoll::getEventsRequest(), epollFd_ = " << epollFd_;
  std::vector<SP_Channel> req_data;
  for (int i = 0; i < events_num; ++i) {
    // 获取有事件产生的描述符
    int fd = events_[i].data.fd;
    LOG << CurrentThread::tid() << " 有事件产生的描述符为: fd = " << fd;
    SP_Channel cur_req = fd2chan_[fd];

    if (cur_req) {
      cur_req->setRevents(events_[i].events);
      cur_req->setEvents(0);
      // 加入线程池之前将Timer和request分离
      // cur_req->seperateTimer();
      req_data.push_back(cur_req);
    } else {
      LOG << "SP cur_req is invalid";
    }
  }
  return req_data;
}

void Epoll::add_timer(SP_Channel request_data, int timeout) {
  LOG << CurrentThread::tid() << " Epoll::add_timer(), epollFd_ = " << epollFd_
      << " fd = " << request_data->getFd();
  shared_ptr<HttpData> t = request_data->getHolder();
  if (t)
    timerManager_.addTimer(t, timeout);
  else
    LOG << "timer add fail";
}