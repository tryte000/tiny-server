#ifndef TINY_WEBSERVER_THREADPOOL_THREADPOOL_H
#define TINY_WEBSERVER_THREADPOOL_THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/lock.hpp"
#include "../cgi-mysql/sql_connection_pool.h"

template <typename T>
class ThreadPool
{
private:
    int thread_number_;             // 线程池中的线程数
    int max_requests_;              // 请求队列中允许的最大请求数
    pthread_t *threads_;            // 描述线程池的数组，其大小为m_thread_number
    std::list<T *> workqueue_;      // 请求队列
    Locker queuelocker_;            // 保护请求队列的互斥锁
    Sem queuestat_;                 // 是否有任务需要处理
    SqlConnectionPool *conn_pool_;  // 数据库
    int actor_model_;               // 模型切换

public:
    ThreadPool(int actor_model, SqlConnectionPool *conn_pool,
        int thread_number = 8, int max_request = 10000);
    ~ThreadPool();

    bool Append(T *request, int state);
    bool AppendP(T *request);

private:
    static void *Worker(void *arg);
    void Run();
};

template <typename T>
ThreadPool<T>::ThreadPool(int actor_model, SqlConnectionPool *conn_pool,
    int thread_number, int max_request) : actor_model_(actor_model),
        thread_number_(thread_number), max_requests_(max_request),
        threads_(NULL),conn_pool_(conn_pool)
{
    if (thread_number <= 0 || max_request <= 0)
    {
        throw std::exception();
    }
    
    this->threads_ = new pthread_t[this->thread_number_];
    if (!this->threads_)
    {
        throw std::exception();
    }
    
    for (int i = 0; i < thread_number; i++)
    {
        if (pthread_create(this->threads_ + i, NULL, this->Worker, this) != 0)
        {
            delete []this->threads_;
            throw std::exception();
        }
        if (pthread_detach(this->threads_[i]))
        {
            delete []this->threads_;
            throw std::exception();
        }
    }
}

template <typename T>
ThreadPool<T>::~ThreadPool()
{
    delete []this->threads_;
}

template <typename T>
bool ThreadPool<T>::Append(T *request, int state)
{
    this->queuelocker_.Lock();
    if (this->workqueue_.size() >= this->max_requests_)
    {
        this->queuelocker_.Unlock();
        return false;
    }
    
    request->state_ = state;
    this->workqueue_.push_back(request);
    this->queuelocker_.Unlock();
    this->queuestat_.Post();
    return true;
}

template <typename T>
bool ThreadPool<T>::AppendP(T *requset)
{
    this->queuelocker_.Lock();
    if (this->workqueue_.size() >= this->max_requests_)
    {
        this->queuelocker_.Unlock();
        return false;
    }
    
    this->workqueue_.push_back(requset);
    this->queuelocker_.Unlock();
    this->queuestat_.Post();
    return true;
}

template <typename T>
void *ThreadPool<T>::Worker(void *arg)
{
    ThreadPool *pool = (ThreadPool *)arg;
    pool->Run();
    return pool;
}

template <typename T>
void ThreadPool<T>::Run()
{
    while (true)
    {
        this->queuestat_.Wait();
        this->queuelocker_.Lock();
        if (this->workqueue_.empty())
        {
            this->queuelocker_.Unlock();
            continue;
        }
        T *request = this->workqueue_.front();
        this->workqueue_.pop_front();
        this->queuelocker_.Unlock();
        if (!request)
        {
            continue;
        }
        if (this->actor_model_ == 1)
        {
            if (request->state_ == 0)
            {
                if (request->ReadOnce())
                {
                    request->improv_ = 1;
                    ConnectionRAII mysqlcon(&request->mysql_, this->conn_pool_);
                    request->Process();
                }
                else
                {
                    request->improv_ = 1;
                    request->timer_flag_ = 1;
                }
            }
            else
            {
                if (request->Write())
                {
                    request->improv_ = 1;
                }
                else
                {
                    request->improv_ = 1;
                    request->timer_flag_ = 1;
                }
            }
        }
        else
        {
            ConnectionRAII mysqlcon(&request->mysql_, this->conn_pool_);
            request->Process();
        }
    }
}

#endif // TINY_WEBSERVER_THREADPOOL_THREADPOOL_H