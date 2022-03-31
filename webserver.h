#ifndef TINY_WEBSERVER_WEBSERVER_H
#define TINY_WEBSERVER_WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./threadpool/threadpool.hpp"
#include "./http/http_conn.h"
#include "./timer/lst_timer.h"

const int MAX_FD = 65536;           // 最大文件描述符
const int MAX_EVENT_NUMBER = 10000; // 最大事件数
const int TIMESLOT = 5;             // 最小超时单位

class WebServer
{

public:
    // 基础
    int port_;
    char *root_;
    int log_write_;
    int close_log_;
    int actormodel_;

    int pipefd_[2];
    int epollfd_;
    HttpConn *users_;

    // 数据库相关
    SqlConnectionPool *conn_pool_;
    string user_;          // 登陆数据库用户名
    string password_;      // 登陆数据库密码
    string database_name_; // 使用数据库名
    int sql_num_;

    // 线程池相关
    ThreadPool<HttpConn> *pool_;
    int thread_num_;

    // epoll_event相关
    epoll_event events_[MAX_EVENT_NUMBER];

    int listenfd_;
    int opt_linger_;
    int trigmode_;
    int listen_trigmode_;
    int conn_trigmode_;

    // 定时器相关
    struct ClientData *users_timer_;
    Utils utils_;

public:
    WebServer();
    ~WebServer();

    void Init(int port , string user, string passWord, string databaseName,
              int log_write , int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model);

    void CreateThreadPool();
    void SqlPool();
    void LogWrite();
    void TrigMode();
    void EventListen();
    void EventLoop();
    void Timer(int connfd, struct sockaddr_in client_address);
    void AdjustTimer(UtilTimer *timer);
    void DealTimer(UtilTimer *timer, int sockfd);
    bool Dealclinetdata();
    bool Dealwithsignal(bool& timeout, bool& stop_server);
    void Dealwithread(int sockfd);
    void Dealwithwrite(int sockfd);
};
#endif // TINY_WEBSERVER_WEBSERVER_H