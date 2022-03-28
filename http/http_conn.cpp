#include "http_conn.h"

#include <mysql/mysql.h>
#include <fstream>
#include <string.h>
#include <error.h>

#include "../lock/lock.hpp"
#include "./http_epoll.h"

// http 响应信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

Locker locker;
map<string, string> users;

int HttpConn::user_count_ = 0;
int HttpConn::epollfd_ = -1;

/**
 * @brief 初始化mysql连接
 * 
 * @param conn_pool 
 */
void HttpConn::InitMysqlResult(SqlConnectionPool *conn_pool)
{
    // 先从连接池取一个连接
    MYSQL *mysql = nullptr;
    ConnectionRAII mysqlconn(&mysql, conn_pool);

    // 在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username, passwd FROM USER limit 1"))
    {
        LOG_ERROR("SELECT error: %s\n", mysql_error(mysql));
    }

    // 从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    // 返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    // 返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_field(result);

    // 获取结果集的数据，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string tmp1(row[0]);
        string tmp2(row[1]);
        users[tmp1] = users[tmp2];
    }
}

/**
 * @brief 关闭连接，关闭一个连接，客户总量减一
 * 
 * @param real_close 
 */
void HttpConn::CloseConn(bool real_close)
{
    if (real_close && (this->sockfd_ != -1))
    {
        printf("close %d\n", this->sockfd_);
        removefd(this->epollfd_, this->sockfd_);
        this->sockfd_ = -1;
        this->user_count_--;
    }
}

/**
 * @brief 初始化连接,外部调用初始化套接字地址
 * 
 * @param sockfd 
 * @param addr 
 * @param root 
 * @param trig_mode 
 * @param close_log 
 * @param user 
 * @param passwd 
 * @param sqlname 
 */
void HttpConn::Init(int sockfd, const sockaddr_in &addr, char *root,
    int trig_mode, int close_log, string user, string passwd, string sqlname)
{
    this->sockfd_ = sockfd;
    this->address_ = addr;
    
    addfd(this->epollfd_, this->sockfd_, true, this->trigmode_);
    this->user_count_++;

    this->doc_root_ = root;
    this->trigmode_ = trig_mode;
    this->close_log_ = close_log;

    strcpy(this->sql_user_, user.c_str());
    strcpy(this->sql_password_, passwd.c_str());
    strcpy(this->sql_name_, sqlname.c_str());

    this->Init();
}

/**
 * @brief 初始化新接受的连接
 *        check_state 默认为分析请求行状态
 * 
 */
void HttpConn::Init()
{
    this->mysql_ = NULL;
    this->bytes_to_send_ = 0;
    this->bytes_have_send_ = 0;
    this->check_state_ = CHECK_STATE_REQUESTLINE;
    this->linger_ = false;
    this->method_ = GET;
    this->url_ = 0;
    this->version_ = 0;
    this->content_length = 0;
    this->host_ = 0;
    this->start_line_ = 0;
    this->checked_idx_ = 0;
    this->read_idx_ = 0;
    this->write_idx_ = 0;
    this->cgi_ = 0;
    this->state_ = 0;
    this->timer_flag_ = 0;
    this->improv_ = 0;

    memset(this->read_buf_, '\0', READ_BUFFER_SIZE);
    memset(this->write_buf_, '\0', WRITE_BUFFER_SIZE);
    memset(this->real_file_, '\0', FILENAME_LEN);
}

/**
 * @brief 从状态机，用于分析出一行内容
 *        返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
 * 
 * @return HttpConn::LINE_STATUS 
 */
HttpConn::LINE_STATUS HttpConn::ParseLine()
{
    char tmp;
    for (; this->checked_idx_ < this->read_idx_; this->checked_idx_++)
    {
        tmp = this->read_buf_[this->checked_idx_];
        if (tmp == '\r')
        {
            if ((this->checked_idx_ + 1) == this->read_idx_)
            {
                return HttpConn::LINE_STATUS::LINE_OPEN;
            }
            else if (this->read_buf_[this->checked_idx_ + 1] == '\n')
            {
                this->read_buf_[this->checked_idx_++] = '\0';
                this->read_buf_[this->checked_idx_++] = '\0';
                return HttpConn::LINE_STATUS::LINE_OK;
            }
            return HttpConn::LINE_STATUS::LINE_BAD;
        }
        else if (tmp == '\n')
        {
            if (this->checked_idx_ > 1 && 
                this->read_buf_[this->checked_idx_ - 1] == '\r') {
                this->read_buf_[this->checked_idx_ - 1] = '\0';
                this->read_buf_[this->checked_idx_++] = '\0';
                return HttpConn::LINE_STATUS::LINE_OK;
            }
            return HttpConn::LINE_STATUS::LINE_BAD;
        }
    }
    return HttpConn::LINE_STATUS::LINE_OPEN;
}

/**
 * @brief 循环读取客户数据，直到无数据可读或对方关闭连接
 *        非阻塞ET工作模式下，需要一次性将数据读完
 * 
 * @return true 
 * @return false 
 */
bool HttpConn::ReadOnce()
{
    if (this->read_idx_ >= HttpConn::READ_BUFFER_SIZE)
    {
        return false;
    }
    
    int bytes_read = 0;
    // LT 读取数据
    if (this->trigmode_ == 0)
    {
        bytes_read = recv(this->sockfd_, this->read_buf_ + this->read_idx_,
            HttpConn::READ_BUFFER_SIZE - this->read_idx_, 0);
        this->read_idx_ += bytes_read;

        if (bytes_read <= 0)
        {
            return false;
        }
        
        return true;
    }
    // ET读数据
    else
    {
        while (true)
        {
            bytes_read = recv(this->sockfd_, this->read_buf_ + this->read_idx_,
                HttpConn::READ_BUFFER_SIZE - this->read_idx_, 0);
            if (bytes_read == -1)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break;
                }
                return false;
            }
            else if (bytes_read == 0)
            {
                return false;
            }
            this->read_idx_ += bytes_read;
        }
        return true;
    }
}

/**
 * @brief 解析http请求行，获得请求方法，目标url及http版本号
 * 
 * @param text 
 * @return HttpConn::HTTP_CODE 
 */
HttpConn::HTTP_CODE HttpConn::ParseRequestLine(char *text)
{
    this->url_ = strpbrk(text, "\t");
    if (!this->url_)
    {
        return HttpConn::HTTP_CODE::BAD_REQUEST;
    }
    *(this->url_++) = '\0';
    
    char *method = text;
    if (strcasecmp(method, "GET") == 0)
    {
        this->method_ = HttpConn::METHOD::GET;
    }
    else if (strcasecmp(method, "POST") == 0)
    {
        this->method_ = HttpConn::METHOD::POST;
        this->cgi_ = 1;
    }
    else
    {
        return HttpConn::HTTP_CODE::BAD_REQUEST;
    }

    this->url_ += strspn(this->url_, " \t");
    this->version_ = strpbrk(this->url_, " \t");
    if (!this->version_)
    {
        return HttpConn::HTTP_CODE::BAD_REQUEST;
    }
    *(this->version_++) = '\0';
    this->version_ += strspn(this->version_, " \t");

    if (strcasecmp(this->version_, "HTTP/1.1") != 0)
    {
        return HttpConn::HTTP_CODE::BAD_REQUEST;
    }
    if (strncasecmp(this->url_, "http://", 7) == 0)
    {
        this->url_ += 7;
        this->url_ = strchr(this->url_, '/');
    }
    if (strncasecmp(this->url_, "https://", 8) == 0)
    {
        this->url_ += 8;
        this->url_ = strchr(this->url_, '/');
    }

    if (!this->url_ || this->url_[0] != '/')
    {
        return HttpConn::HTTP_CODE::BAD_REQUEST;
    }
    //当url为/时，显示判断界面
    if (strlen(this->url_) == 1)
    {
        strcat(this->url_, "judge.html");
    }
    this->check_state_ = HttpConn::CHECK_STATE::CHECK_STATE_HEADER;
    return HttpConn::HTTP_CODE::NO_REQUEST;
}