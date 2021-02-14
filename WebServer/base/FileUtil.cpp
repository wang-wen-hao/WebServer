#include "FileUtil.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;

AppendFile::AppendFile(string filename) : fp_(fopen(filename.c_str(), "ae")) {
  // 用户提供缓冲区
  // https://www.cnblogs.com/xiongxinxzy/p/13622268.html
  /*
  所谓带不带缓冲，指的是在调用系统调用如write或read时是否有**用户级的缓冲**
  所谓不带缓冲，并不是指内核不提供缓冲，而是只单纯的系统调用，不是函数库的调用。系统内核对磁盘的读写
  都会提供一个块缓冲（在有些地方也被称为内核高速缓存），当用write函数对其写数据时，直接调用系统调用
  ，将数据写入到块缓冲进行排队，当块缓冲达到一定的量时，才会把数据写入磁盘。因此所谓的不带缓冲的I/O
  是指进程不提供缓冲功能（但内核还是提供缓冲的）。每调用一次write或read函数，直接系统调用。

  而带缓冲的I/O是指进程对输入输出流进行了改进，提供了一个流缓冲，当用fwrite函数网磁盘写数据时，
  先把数据写入流缓冲区中，当达到一定条件，比如**流缓冲区满了**，或**刷新流缓冲**，这时候才会把数据
  一次送往内核提供的块缓冲，再经块缓冲写入磁盘。（双重缓冲）
  因此，带缓冲的I/O在往磁盘写入相同的数据量时，会比不带缓冲的I/O调用系统调用的次数要少。

  读取磁盘的性能取决于磁盘的使用时间，比如一个磁盘如果一直处于读取状态，那么你再多的线程也没有用。
  */
  setbuffer(fp_, buffer_, sizeof buffer_);
}

AppendFile::~AppendFile() { fclose(fp_); }

void AppendFile::append(const char* logline, const size_t len) {
  size_t n = this->write(logline, len);
  size_t remain = len - n;
  while (remain > 0) {
    size_t x = this->write(logline + n, remain);
    if (x == 0) {
      int err = ferror(fp_);
      if (err) fprintf(stderr, "AppendFile::append() failed !\n");
      break;
    }
    n += x;
    remain = len - n;
  }
}

void AppendFile::flush() { fflush(fp_); }

size_t AppendFile::write(const char* logline, size_t len) {
  // fwrite 和 fwrite_unlocked是一对，其中fwrite_unlocked是fwrite的线程不安全版本，因为不加锁
  // 无锁版本明显要快很多
  return fwrite_unlocked(logline, 1, len, fp_);
}