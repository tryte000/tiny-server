#ifndef TINY_WEBSERVER_CONFIG_H
#define TINY_WEBSERVER_CONFIG_H

#include <unistd.h>
#include <stdlib.h>

class Config
{
private:
    /* data */
public:
    Config();
    ~Config()=default;

    void ParseArg(int argc, char *argv[]);

    // 端口号
    int port_;

    // 日志写入方式
    int logwrite_;

    // 触发组合模式
    int trigmode_;

    // listenfd触发模式
    int listen_trigmode_;

    // connfd触发模式
    int conn_trigmode_;

    // 优雅关闭链接
    int opt_linger_;

    // 数据库连接池数量
    int sql_num_;

    // 线程池内的线程数量
    int thread_num_;

    // 是否关闭日志
    int close_log_;

    // 并发模型选择
    int actor_mode_;
};


#endif // TINY_WEBSERVER_CONFIG_H