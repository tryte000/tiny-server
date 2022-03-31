#include "http_conn.h"

#include <mysql/mysql.h>
#include <fstream>
#include <string.h>
#include <error.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/uio.h>

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
    this->content_length_ = 0;
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

/**
 * @brief 解析http请求的一个头部信息
 * 
 * @param text 
 * @return HttpConn::HTTP_CODE 
 */
HttpConn::HTTP_CODE HttpConn::ParseHeaders(char *text)
{
    if (text[0] == '\0')
    {
        if (this->content_length_ != 0)
        {
            this->check_state_ = HttpConn::CHECK_STATE::CHECK_STATE_CONTENT;
            return HttpConn::HTTP_CODE::NO_REQUEST;
        }
        return HttpConn::HTTP_CODE::GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, "\t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            this->linger_ = true;
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, "\t");
        this->content_length_ = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, "\t");
        this->host_ = text;
    }
    else
    {
        LOG_INFO("oop! unknow header: %s", text);
    }
    return HttpConn::HTTP_CODE::NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::ParseContent(char *text)
{
    if (this->read_idx_ >= this->content_length_ + this->checked_idx_)
    {
        text[this->content_length_] = '\0';
        // POST 请求中最后为输入用户的账号和密码
        this->string_ = text;
        return HttpConn::HTTP_CODE::GET_REQUEST;
    }
    return HttpConn::HTTP_CODE::NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::ProcessRead()
{
    HttpConn::LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    while ((this->check_state_ == CHECK_STATE_CONTENT && line_status == LINE_OK)
        || (line_status == this->ParseLine()) == LINE_OK)
    {
        text = this->GetLine();
        this->start_line_ = this->checked_idx_;
        LOG_INFO("%s", text);
        switch (this->check_state_)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = this->ParseRequestLine(text);
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            break;
        }
        case CHECK_STATE_HEADER:
        {
            ret = this->ParseHeaders(text);
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            else if (ret == GET_REQUEST)
            {
                return this->DoRequest();
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            ret = this->ParseContent(text);
            if (ret == GET_REQUEST)
            {
                return this->DoRequest();
            }
            line_status = LINE_OPEN;
            break;
        }
        default:
            return HTTP_CODE::INTERNAL_ERROR;
            break;
        }
    }
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::DoRequest()
{
    strcpy(this->real_file_, this->doc_root_);
    int len = strlen(this->doc_root_);

    const char *p = strrchr(this->url_, '/');

    // 处理cgi
    if (this->cgi_ == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        // 根据标志判断是登录检测还是注册检测
        char flag = this->url_[1];

        char *url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(url_real, "/");
        strcat(url_real, this->url_ + 2);
        strncpy(this->real_file_ + len, url_real, FILENAME_LEN - len - 1);
        free(url_real);

        // 将用户名和密码提取出来
        char name[100], passwd[100];
        int i;
        for (i = 5; this->string_[i] != '&'; i++)
        {
            name[i - 5] = this->string_[i];
        }
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; this->string_[i] != '\0'; ++i, ++j)
        {
            passwd[j] = this->string_[i];
        }
        passwd[j] = '\0';

        if (*(p + 1) == '3')
        {
            // 如果是注册，先检测数据库中是否有重名的
            // 没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, passwd);
            strcat(sql_insert, "')");

            if (this->users_.find(name) == users.end())
            {
                locker.Lock();
                int res = mysql_query(this->mysql_, sql_insert);
                users.insert(pair<string, string>(name, passwd));
                locker.Unlock();

                if (!res)
                {
                    strcpy(this->url_, "/log.html");
                }
                else
                {
                    strcpy(this->url_, "/registerError.html");
                }
            }
            else
            {
                strcpy(this->url_, "/registerError.html");
            }
        }
        // 如果是登录，直接判断
        // 若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2')
        {
            if (users.find(name) != users.end() && users[name] == passwd)
            {
                strcpy(this->url_, "/welcome.html");
            }
            else
            {
                strcpy(this->url_, "/logError.html");
            }
        }
    }

    if (*(p + 1) == '0')
    {
        char *url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(url_real, "/register.html");
        strncpy(this->real_file_ + len, url_real, strlen(url_real));
        free(url_real);
    }
    else if (*(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(this->real_file_ + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(this->real_file_ + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(this->real_file_ + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(this->real_file_ + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else
    {
        strncpy(this->real_file_ + len, this->url_, FILENAME_LEN - len - 1);
    }

    if (stat(this->real_file_, &this->file_stat_) < 0)
    {
        return NO_RESOURCE;
    }

    if (!(this->file_stat_.st_mode & S_IROTH))
    {
        return FORBIDDEN_REQUEST;
    }

    if (S_ISDIR(this->file_stat_.st_mode))
        return BAD_REQUEST;

    int fd = open(this->real_file_, O_RDONLY);
    this->file_address_ = (char *)mmap(0, this->file_stat_.st_size,
        PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

void HttpConn::Unmap()
{
    if (this->file_address_)
    {
        munmap(this->file_address_, this->file_stat_.st_size);
        this->file_address_ = 0;
    }
}

bool HttpConn::Write()
{
    int tmp = 0;

    if (this->bytes_to_send_ == 0)
    {
        modfiyfd(this->epollfd_, this->sockfd_, EPOLLIN, this->trigmode_);
        this->Init();
        return true;
    }
    
    while (1)
    {
        tmp = writev(this->sockfd_, this->iv_, this->iv_count_);

        if (tmp < 0)
        {
            if (errno == EAGAIN)
            {
                modfiyfd(this->epollfd_, this->sockfd_, EPOLLOUT,
                    this->trigmode_);
                return true;
            }
            this->Unmap();
            return false;
        }
        this->bytes_have_send_ += tmp;
        this->bytes_to_send_ -= tmp;

        if (this->bytes_have_send_ >= this->iv_[0].iov_len)
        {
            this->iv_[0].iov_len = 0;
            this->iv_[1].iov_base = this->file_address_ +
                (this->bytes_have_send_ - this->write_idx_);
            this->iv_[1].iov_len = this->bytes_to_send_;
        }
        else
        {
            this->iv_[0].iov_base = this->write_buf_ + this->bytes_have_send_;
            this->iv_[0].iov_len = this->iv_[0].iov_len + this->bytes_have_send_;
        }

        if (this->bytes_to_send_ <= 0)
        {
            this->Unmap();
            modfiyfd(this->epollfd_, this->sockfd_, EPOLLIN, this->trigmode_);

            if (this->linger_)
            {
                this->Init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

bool HttpConn::AddResponse(const char *format, ...)
{
    if (this->write_idx_ >= WRITE_BUFFER_SIZE)
    {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(this->write_buf_ + this->write_idx_,
        WRITE_BUFFER_SIZE - 1 - this->write_idx_, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - this->write_idx_))
    {
        va_end(arg_list);
        return false;
    }
    this->write_idx_ += len;
    va_end(arg_list);

    LOG_INFO("request:%s", this->write_buf_);

    return true;
}

bool HttpConn::AddStatusLine(int status, const char *title)
{
    return this->AddResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool HttpConn::AddHeaders(int content_len)
{
    return this->AddContentLength(content_len) && this->AddLinger() &&
        this->AddBlankLine();
}

bool HttpConn::AddContentLength(int content_len)
{
    return this->AddResponse("Content-Length:%d\r\n", content_len);
}

bool HttpConn::AddContentType()
{
    return this->AddResponse("Content-Type:%s\r\n", "text/html");
}

bool HttpConn::AddLinger()
{
    return this->AddResponse("Connection:%s\r\n", (this->linger_ == true) ?
        "keep-alive" : "close");
}

bool HttpConn::AddBlankLine()
{
    return this->AddResponse("%s", "\r\n");
}
bool HttpConn::AddContent(const char *content)
{
    return this->AddResponse("%s", content);
}

bool HttpConn::ProcessWrite(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR:
    {
        this->AddStatusLine(500, error_500_title);
        this->AddHeaders(strlen(error_500_form));
        if (!this->AddContent(error_500_form))
        {
            return false;
        }
        break;
    }
    case BAD_REQUEST:
    {
        this->AddStatusLine(404, error_404_title);
        this->AddHeaders(strlen(error_404_form));
        if (!this->AddContent(error_404_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        this->AddStatusLine(403, error_403_title);
        this->AddHeaders(strlen(error_403_form));
        if (!this->AddContent(error_403_form))
        {
            return false;
        }
        break;
    }
    case FILE_REQUEST:
    {
        this->AddStatusLine(200, ok_200_title);
        if (this->file_stat_.st_size != 0)
        {
            this->AddHeaders(this->file_stat_.st_size);
            this->iv_[0].iov_base = this->write_buf_;
            this->iv_[0].iov_len = this->write_idx_;
            this->iv_[1].iov_base = this->file_address_;
            this->iv_[1].iov_len = this->file_stat_.st_size;
            this->iv_count_ = 2;
            this->bytes_to_send_ = this->write_idx_ + this->file_stat_.st_size;
            return true;
        }
        else
        {
            const char *ok_string = "<html><body></body></html>";
            this->AddHeaders(strlen(ok_string));
            if (!this->AddContent(ok_string))
            {
                return false;
            }
        }
    }
    default:
        return false;
    }
    this->iv_[0].iov_base = this->write_buf_;
    this->iv_[0].iov_len = this->write_idx_;
    this->iv_count_ = 1;
    this->bytes_to_send_ = this->write_idx_;
    return true;
}

void HttpConn::Process()
{
    HTTP_CODE read_ret = this->ProcessRead();
    if (read_ret == NO_REQUEST)
    {
        modfiyfd(this->epollfd_, this->sockfd_, EPOLLIN, this->trigmode_);
        return;
    }
    bool write_ret = this->ProcessWrite(read_ret);
    if (!write_ret)
    {
        this->CloseConn();
    }
    modfiyfd(this->epollfd_, this->sockfd_, EPOLLOUT, this->trigmode_);
}