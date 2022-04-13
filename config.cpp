#include "config.h"

Config::Config()
{
    // 端口号,默认9006
    this->port_ = 9006;

    // 日志写入方式，默认同步
    this->logwrite_ = 0;

    // 触发组合模式,默认listenfd LT + connfd LT
    this->trigmode_ = 0;

    // listenfd 触发模式，默认LT
    this->listen_trigmode_ = 0;

    // 优雅关闭链接，默认不使用
    this->opt_linger_ = 0;

    // 数据库连接池数量,默认8
    this->sql_num_ = 1;

    // 线程池内的线程数量,默认8
    this->thread_num_ = 1;

    // 关闭日志,默认不关闭
    this->close_log_ = 0;

    // 并发模型,默认是proactor
    this->actor_mode_ = 0;
}

void Config::ParseArg(int argc, char *argv[])
{
    int opt;

    const char *str = "p:l:m:o:s:t:c:a:";

    while ((opt = getopt(argc, argv, str)) != -1)
    {
        switch (opt)
        {
        case 'p':
        {
            this->port_ = atoi(optarg);
            break;
        }
        case 'l':
        {
            this->logwrite_ = atoi(optarg);
            break;
        }
        case 'm':
        {
            this->trigmode_ = atoi(optarg);
            break;
        }
        case 'o':
        {
            this->opt_linger_ = atoi(optarg);
            break;
        }
        case 's':
        {
            this->sql_num_ = atoi(optarg);
            break;
        }
        case 't':
        {
            this->thread_num_ = atoi(optarg);
            break;
        }
        case 'c':
        {
            this->close_log_ = atoi(optarg);
            break;
        }
        case 'a':
        {
            this->actor_mode_ = atoi(optarg);
            break;
        }
        default:
            break;
        }
    }
    
}