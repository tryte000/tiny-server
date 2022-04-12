#ifndef TINY_WEBSERVER_CGI_MYSQL_SQL_CONNECTION_POOL_H
#define TINY_WEBSERVER_CGI_MYSQL_SQL_CONNECTION_POOL_H

#include <string>
#include <mysql/mysql.h>
#include <list>

#include "../lock/lock.hpp"
#include "../log/log.h"

using namespace std;

class SqlConnectionPool
{
public:
    string url_;           // 主机地址
    string port_;          // 数据库端口号
    string user_;          // 登陆数据库用户名
    string password_;      // 登陆数据库密码
    string database_name_; // 使用数据库名
    int close_log_;        // 日志开关

private:
    int maxconn_;  // 最大连接数
    int curconn_;  // 当前已使用的连接数
    int freeconn_; // 当前空闲的连接数
    Locker lock_;
    list<MYSQL *> connlist_; // 连接池
    Sem reserve_;

    // 单例模式
    static SqlConnectionPool *obj_;

public:
    MYSQL *GetConnection();              // 获取数据库连接
    bool ReleaseConnection(MYSQL *conn); // 释放连接
    int GetFreeConn();                   // 获取连接
    void DestroyPool();                  // 销毁所有连接

    // 单例模式
    static SqlConnectionPool *GetInstance();

    void init(string url, string user, string password, string database_name,
        int port, int max_conn, int close_log);

private:
    SqlConnectionPool();
    ~SqlConnectionPool();
    SqlConnectionPool(const SqlConnectionPool &) = delete;
    SqlConnectionPool operator=(const SqlConnectionPool &) = delete;
};

class ConnectionRAII
{
private:
    MYSQL *conn_raii_;
    SqlConnectionPool *pool_raii;

public:
    ConnectionRAII(MYSQL **conn, SqlConnectionPool *conn_pool);
    ~ConnectionRAII();
};

#endif // TINY_WEBSERVER_CGI_MYSQL_SQL_CONNECTION_POOL_H