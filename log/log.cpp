#include <time.h>
#include <sys/time.h>
#include <string.h>
#include "log.h"

using namespace ::std;

Log *Log::instance_ = nullptr;

Log::Log()
{
    this->count_ = 0;
    this->is_async_ = false;
}

Log::~Log()
{
    if (this->fp_ != nullptr)
    {
        fclose(this->fp_);
    }
}

/**
 * @brief 异步需要设置阻塞队列的长度，同步不需要设置
 * 
 * @param file_name 
 * @param close_log 
 * @param log_buf_size 
 * @param split_lines 
 * @param max_queue_size 
 * @return true 
 * @return false 
 */
bool Log::Init(const char *file_name, int close_log, int log_buf_size,
    int split_lines, int max_queue_size) {
    // 如果设置了 max_queue_size，则设置为异步
    if (max_queue_size >= 1)
    {
        this->is_async_ = true;
        this->log_queue_ = new BlockQueue<string>(max_queue_size);
        pthread_t tid;
        // FlushLogThread 为回调函数，这里表示创建线程异步写日志
        pthread_create(&tid, NULL, this->FlushLogThread, NULL);
    }
    
    this->close_log_ = close_log;
    this->log_buf_size_ = log_buf_size;
    this->buf_ = new char[this->log_buf_size_];
    memset(this->buf_, '\0', this->log_buf_size_);
    this->split_lines_ = split_lines;

    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    const char *p = strchr(file_name, '/');
    char log_full_name[256] = {0};

    if (p == NULL)
    {
        snprintf(log_full_name, 255, "%d-%02d-%02d-%s", my_tm.tm_year + 1900,
            my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else
    {
        strcpy(this->log_name_, p + 1);
        strncpy(this->dir_name_, file_name, p - file_name + 1);
        snprintf(log_full_name, 255, "%s%d-%02d-%02d-%s", this->dir_name_,
            my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, this->log_name_);
    }

    this->today_ = my_tm.tm_mday;

    this->fp_ = fopen(log_full_name, "a");
    if (this->fp_ == NULL)
    {
        return false;
    }

    return true;
}

/**
 * @brief 日志写入
 * 
 * @param level 
 * @param format 
 * @param ... 
 */
void Log::WriteLog(int level, const char *format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};

    switch (level)
    {
    case 0:
    {
        strcpy(s, "[debug]:");
        break;
    }
    case 1:
    {
        strcpy(s, "[info]:");
        break;
    }
    case 2:
    {
        strcpy(s, "[warn]:");
        break;
    }
    case 3:
    {
        strcpy(s, "[erro]:");
        break;
    }
    default:
    {
        strcpy(s, "[info]:");
        break;
    }
    }

    this->mutex_.Lock();
    this->count_++;

    // everyday log
    if (this->today_ != my_tm.tm_mday || this->count_ % this->split_lines_ == 0)
    {
        char new_log[256] = {0};
        fflush(this->fp_);
        fclose(this->fp_);
        char tail[16] = {0};

        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900,
            my_tm.tm_mon + 1, my_tm.tm_mday);

        if (this->today_ != my_tm.tm_mday)
        {
            snprintf(new_log, 255, "%s%s%s", this->dir_name_, tail,
                this->log_name_);

            this->today_ = my_tm.tm_mday;
            this->count_ = 0;
        }
        else
        {
            snprintf(new_log, 255, "%s%s%s.%lld", this->dir_name_, tail,
                this->log_name_, this->count_ / this->split_lines_);
        }
        
        this->fp_ = fopen(new_log, "a");
    }

    this->mutex_.Unlock();

    va_list valst;
    va_start(valst, format);
    
    string log_str;
    this->mutex_.Lock();

    // 写入的具体时间
    int n = snprintf(this->buf_, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s",
        my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, my_tm.tm_hour,
            my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);

    int m = vsnprintf(this->buf_ + n, this->log_buf_size_ - 1, format, valst);

    this->buf_[n + m] = '\n';
    this->buf_[n + m + 1] = '\0';
    log_str = this->buf_;

    this->mutex_.Unlock();

    if (this->is_async_ && !this->log_queue_->Full())
    {
        this->log_queue_->Push(log_str);
    }
    else
    {
        this->mutex_.Lock();
        fputs(log_str.c_str(), this->fp_);
        this->mutex_.Unlock();
    }

    va_end(valst);
}

void Log::Flush()
{
    this->mutex_.Lock();
    fflush(this->fp_);
    this->mutex_.Unlock();
}