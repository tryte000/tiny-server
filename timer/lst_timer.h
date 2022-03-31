#ifndef TINY_WEBSERVER_TIMER_LST_TIMER_H
#define TINY_WEBSERVER_TIMER_LST_TIMER_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <time.h>

#include "../log/log.h"

class UtilTimer;

struct ClientData
{
    sockaddr_in address;
    int sockfd;
    UtilTimer *timer;
};

class UtilTimer
{
public:
    time_t expire_;
    void (*cb_func_)(ClientData *);
    ClientData *user_data_;
    UtilTimer *prev_;
    UtilTimer *next_;

public:
    UtilTimer() : prev_(NULL), next_(NULL) {}
};

class SortTimerLst
{
private:
    UtilTimer *head_;
    UtilTimer *tail_;

private:
    void AddTimer(UtilTimer *timer, UtilTimer *lst_head);

public:
    SortTimerLst();
    ~SortTimerLst();

    void AddTimer(UtilTimer *timer);
    void AdjustTimer(UtilTimer *timer);
    void DelTimer(UtilTimer *timer);
    void Tick();
};

class Utils
{
public:
    static int *pipefd_;
    SortTimerLst timer_lst_;
    static int epoolfd_;
    int time_slot_;

public:
    Utils();
    ~Utils();

    void init(int timeslot);

    // 对文件描述符设置阻塞
    int SetNonblocking(int fd);

    // 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void Addfd(int epollfd, int fd, bool one_shot, int trig_mode);

    // 信号处理函数
    static void SigHandler(int sig);

    // 设置信号函数
    void AddSig(int sig, void(handler)(int), bool restart = true);

    // 定时处理任务，重新定时以不断触发SIGALRM信号
    void TimerHandler();

    void ShowError(int connfd, const char *info);
};

void cb_func(ClientData *user_data);

#endif // TINY_WEBSERVER_TIMER_LST_TIMER_H