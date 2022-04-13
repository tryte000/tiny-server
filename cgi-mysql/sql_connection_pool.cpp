#include "sql_connection_pool.h"

SqlConnectionPool *SqlConnectionPool::obj_ = nullptr;

SqlConnectionPool::SqlConnectionPool()
{
    this->curconn_ = 0;
    this->freeconn_ = 0;
}

SqlConnectionPool::~SqlConnectionPool()
{
    this->DestroyPool();
}

SqlConnectionPool *SqlConnectionPool::GetInstance()
{
    if (SqlConnectionPool::obj_ == nullptr)
    {
        SqlConnectionPool::obj_ = new SqlConnectionPool;
        return SqlConnectionPool::obj_;
    }
    return SqlConnectionPool::obj_;
	// static SqlConnectionPool conn_pool;
	// return &conn_pool;
}

/**
 * @brief 构造初始化
 * 
 * @param url 
 * @param user 
 * @param password 
 * @param dbname 
 * @param port 
 * @param maxconn 
 * @param close_log 
 */
void SqlConnectionPool::init(string url, string user, string password, 
    string dbname, int port, int maxconn, int close_log)
{
    this->url_ = url;
    this->port_ = port;
    this->user_ = user;
    this->password_ = password;
    this->database_name_ = dbname;
    this->close_log_ = close_log;

    for (int i = 0; i < maxconn; i++)
    {
        MYSQL *conn = nullptr;
        conn = mysql_init(conn);

        if (conn == nullptr)
        {
            LOG_ERROR("MySql Error");
            exit(1);
        }
        conn = mysql_real_connect(conn, url.c_str(), user.c_str(),
            password.c_str(), dbname.c_str(), port, NULL, 0);

        if (conn == nullptr)
        {
            LOG_ERROR("Mysql Error");
            exit(1);
        }
        this->connlist_.push_back(conn);
        ++this->freeconn_;
    }
    
    this->reserve_ = Sem(this->freeconn_);
    this->maxconn_ = this->freeconn_;
}

/**
 * @brief 获取 mysql 连接
 * 
 * @return MYSQL* 
 */
MYSQL *SqlConnectionPool::GetConnection()
{
    MYSQL *conn = nullptr;

    if (this->connlist_.size() == 0)
    {
        return nullptr;
    }
    
    this->reserve_.Wait();
    this->lock_.Lock();

    conn = this->connlist_.front();
    this->connlist_.pop_front();

    --this->freeconn_;
    ++this->curconn_;

    this->lock_.Unlock();
    return conn;
}

/**
 * @brief 归还连接到连接池
 * 
 * @param conn 
 * @return true 
 * @return false 
 */
bool SqlConnectionPool::ReleaseConnection(MYSQL *conn)
{
    if (conn == nullptr)
    {
        return false;
    }
    
    this->lock_.Lock();

    this->connlist_.push_back(conn);
    ++this->freeconn_;
    --this->curconn_;

    this->lock_.Unlock();

    this->reserve_.Post();
    return true;
}

/**
 * @brief 销毁数据库连接池
 * 
 */
void SqlConnectionPool::DestroyPool()
{
    this->lock_.Lock();
    if (this->connlist_.size() > 0)
    {
        list<MYSQL *>::iterator it;
        for (it = this->connlist_.begin(); it != this->connlist_.end(); it++)
        {
            MYSQL *conn = *it;
            mysql_close(conn);
        }
        
        this->curconn_ = 0;
        this->freeconn_ = 0;
        this->connlist_.clear();
    }
    
    this->lock_.Unlock();
}

int SqlConnectionPool::GetFreeConn()
{
    return this->freeconn_;
}

ConnectionRAII::ConnectionRAII(MYSQL **sql, SqlConnectionPool *conn_pool)
{
    *sql = conn_pool->GetConnection();

    this->conn_raii_ = *sql;
    this->pool_raii = conn_pool;
}

ConnectionRAII::~ConnectionRAII()
{
    this->pool_raii->ReleaseConnection(this->conn_raii_);
}