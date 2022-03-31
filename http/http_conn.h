#ifndef TINY_WEBSERVER_HTTP_HTTP_CONN_H
#define TINY_WEBSERVER_HTTP_HTTP_CONN_H

#include <netinet/in.h>
#include <sys/stat.h>
#include <map>
#include <string>
#include <mysql/mysql.h>

#include "../cgi-mysql/sql_connection_pool.h"

using namespace std;

class HttpConn
{
public:
    static const int FILENAME_LEN = 20;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    enum METHOD 
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSE_CONNECTION
    };
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

private:
    int sockfd_;
    sockaddr_in address_;
    char read_buf_[READ_BUFFER_SIZE];
    int read_idx_;
    int checked_idx_;
    int start_line_;
    char write_buf_[WRITE_BUFFER_SIZE];
    int write_idx_;
    enum CHECK_STATE check_state_;
    enum METHOD method_;
    char real_file_[FILENAME_LEN];
    char *url_;
    char *version_;
    char *host_;
    int content_length_;
    bool linger_;
    char *file_address_;
    struct stat file_stat_;
    struct iovec iv_[2];
    int iv_count_;
    int cgi_;       // 是否启用的POST
    char *string_;  // 存储请求头数据
    int bytes_to_send_;
    int bytes_have_send_;
    char *doc_root_;

    map<string, string> users_;
    int trigmode_;
    int close_log_;

    char sql_user_[100];
    char sql_password_[100];
    char sql_name_[100];

public:
    static int epollfd_;
    static int user_count_;
    MYSQL *mysql_;
    int state_; // 读为0, 写为1
    int timer_flag_;
    int improv_;

private:
    void Init();
    enum HTTP_CODE ProcessRead();
    bool ProcessWrite(enum HTTP_CODE ret);
    enum HTTP_CODE ParseRequestLine(char *text);
    enum HTTP_CODE ParseHeaders(char *text);
    enum HTTP_CODE ParseContent(char *text);
    enum HTTP_CODE DoRequest();
    char *GetLine()
    {
        return this->read_buf_ + this->start_line_;
    }
    enum LINE_STATUS ParseLine();
    void Unmap();
    bool AddResponse(const char *format, ...);
    bool AddContent(const char *content);
    bool AddStatusLine(int status, const char *title);
    bool AddHeaders(int content_length);
    bool AddContentType();
    bool AddContentLength(int content_length);
    bool AddLinger();
    bool AddBlankLine();


public:
    HttpConn() = default;
    ~HttpConn() = default;

    void Init(int sockfd, const sockaddr_in &addr, char *, int, int,
        string user, string password, string sqlname);
    void CloseConn(bool real_close = false);
    void Process();
    bool ReadOnce();
    bool Write();
    sockaddr_in *GetAddress()
    {
        return &this->address_;
    }
    void InitMysqlResult(SqlConnectionPool *conn_pool);
};



#endif // TINY_WEBSERVER_HTTP_HTTP_CONN_H