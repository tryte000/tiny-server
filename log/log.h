#ifndef TINY_WEBSERVER_LOG_LOG_H
#define TINY_WEBSERVER_LOG_LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.hpp"

using namespace std;

class Log
{
private:
    char dir_name_[128]; // 路径名
    char log_name_[128]; // log文件名
    int split_lines_;    // 日志最大行数
    int log_buf_size_;   // 日志缓冲区大小
    long long count_;    // 日志行数记录
    int today_;          // 因为按天分类,记录当前时间是那一天
    FILE *fp_;           // 打开log的文件指针
    char *buf_;
    BlockQueue<string> *log_queue_; // 阻塞队列
    bool is_async_; // 是否同步标志位
    Locker mutex_;
    int close_log_; // 关闭日志

    static Log *instance_;

public:
    static Log *GetInstance()
    {
        if (Log::instance_ == nullptr)
        {
            Log::instance_ = new Log();
        }
        return Log::instance_;
        // static Log instance;
        // return &instance;
    }

    static void *FlushLogThread(void *args)
    {
        Log::GetInstance()->asyncWriteLog();
        return nullptr;
    }

    bool Init(const char *file_name, int close_log, int log_buf_size = 8192,
        int split_lines = 5000000, int max_queue_size = 0);

    void WriteLog(int level, const char *format, ...);

    void Flush(void);

private:
    Log();
    ~Log();
    Log(const Log &) = delete;
    Log &operator=(const Log &) = delete;

    void asyncWriteLog()
    {
        string single_log;
        // 从阻塞队列中取出一个日志string，写入文件
        while (this->log_queue_->Pop(single_log))
        {
            this->mutex_.Lock();
            fputs(single_log.c_str(), this->fp_);
            this->mutex_.Unlock();
        }
        
    }
};

#define LOG_DEUBG(format, ...) do { \
    if (this->close_log_) \
    { \
        Log::GetInstance()->WriteLog(0, format, ##__VA_ARGS__); \
        Log::GetInstance()->Flush(); \
    } \
} while(0)

#define LOG_INFO(format, ...) do { \
    if (this->close_log_) \
    { \
        Log::GetInstance()->WriteLog(1, format, ##__VA_ARGS__); \
        Log::GetInstance()->Flush(); \
    } \
} while(0)

#define LOG_WARN(format, ...) do { \
    if (this->close_log_) \
    { \
        Log::GetInstance()->WriteLog(2, format, ##__VA_ARGS__); \
        Log::GetInstance()->Flush(); \
    } \
} while(0)

#define LOG_ERROR(format, ...) do { \
    if (this->close_log_) \
    { \
        Log::GetInstance()->WriteLog(3, format, ##__VA_ARGS__); \
        Log::GetInstance()->Flush(); \
    } \
} while(0)

#endif // TINY_WEBSERVER_LOG_LOG_H