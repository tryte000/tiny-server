#include <string>

#include "webserver.h"
#include "config.h"

using namespace std;

int main(int argc, char *argv[])
{
    //需要修改的数据库信息,登录名,密码,库名
    string user = "root";
    string passwd = "123456";
    string databasename = "test";

    //命令行解析
    Config config;
    config.ParseArg(argc, argv);

    WebServer server;

    // 初始化
    server.Init(config.port_, user, passwd, databasename, config.logwrite_,
        config.opt_linger_, config.trigmode_, config.sql_num_,
        config.thread_num_, 
        config.close_log_, config.actor_mode_);
    

    // 日志
    server.LogWrite();

    // 数据库
    server.SqlPool();

    // 线程池
    server.CreateThreadPool();

    // 触发模式
    server.TrigMode();

    // 监听
    server.EventListen();

    // 运行
    server.EventLoop();

    return 0;
}