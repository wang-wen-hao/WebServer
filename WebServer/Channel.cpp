#include "Channel.h"

#include <unistd.h>
#include <cstdlib>
#include <iostream>

#include <queue>

#include "Epoll.h"
#include "EventLoop.h"
#include "Util.h"

using namespace std;

Channel::Channel(EventLoop *loop)
    : loop_(loop), events_(0), lastEvents_(0), fd_(0) {
      LOG << CurrentThread::tid() << " loop_'s tid = " << loop_->getLoopTid() 
      << " Channel constructed" << " fd_ = " << fd_;
    }

Channel::Channel(EventLoop *loop, int fd)
    : loop_(loop), fd_(fd), events_(0), lastEvents_(0) {
      LOG << CurrentThread::tid() << " loop_'s tid = " << loop_->getLoopTid() 
      << " Channel constructed" << " fd_ = " << fd_;
    }

Channel::~Channel() {
  // loop_->poller_->epoll_del(fd, events_);
  // close(fd_);
  LOG << CurrentThread::tid() << " loop_'s tid = " << loop_->getLoopTid() 
  << " Channel destroyed! fd_ = " << fd_;
}

int Channel::getFd() { return fd_; }
void Channel::setFd(int fd) { fd_ = fd; }

void Channel::handleRead() {
  LOG << CurrentThread::tid() << " Channel::handleRead()";
  if (readHandler_) {
    readHandler_();
  }
}

void Channel::handleWrite() {
  LOG << "Channel::handleWrite()";
  if (writeHandler_) {
    writeHandler_();
  }
}

void Channel::handleConn() {
  LOG << CurrentThread::tid() << " Channel::handleConn()";
  if (connHandler_) {
    connHandler_();
  }
}