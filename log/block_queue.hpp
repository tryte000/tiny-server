#ifndef TINY_WEBSERVER_LOG_BLOCK_QUEUE_H
#define TINY_WEBSERVER_LOG_BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/lock.hpp"

using namespace std;

template <typename T>
class BlockQueue
{
private:
    Locker mutex_;
    Cond cond_;

    T *array_;
    int size_;
    int max_size_;
    int front_;
    int back_;

public:
    BlockQueue(int max_size = 1000)
    {
        if (max_size <= 0)
        {
            exit(-1);
        }

        this->max_size_ = max_size;
        this->array_ = new T[max_size];
        this->size_ = 0;
        this->front_ = -1;
        this->back_ = -1;
    }

    ~BlockQueue()
    {
        this->mutex_.Lock();
        if (this->array_ != nullptr)
        {
            delete []this->array_;
        }
        this->mutex_.Unlock();
    }

    void Clear()
    {
        this->mutex_.Lock();
        this->size_ = 0;
        this->front_ = -1;
        this->back_ = -1;
        this->mutex_.Unlock();
    }

    /**
     * @brief 判断队列是否满了
     * 
     * @return true 
     * @return false 
     */
    bool Full()
    {
        this->mutex_.Lock();
        if (this->size_ = this->max_size_)
        {
            this->mutex_.Unlock();
            return true;
        }
        this->mutex_.Unlock();
        return false;
    }

    /**
     * @brief 判断队列是否为空
     * 
     * @return true 
     * @return false 
     */
    bool Empty()
    {
        this->mutex_.Lock();
        if (this->size_ == 0)
        {
            this->mutex_.Unlock();
            return true;
        }
        this->mutex_.Unlock();
        return false;
    }

    /**
     * @brief 返回队首元素
     * 
     * @param value 
     * @return true 
     * @return false 
     */
    bool Front(T &value)
    {
        this->mutex_.Lock();
        if (this->size_ == 0)
        {
            this->mutex_.Unlock();
            return false;
        }
        value = this->array_[this->front_];
        this->mutex_.Unlock();
        return true;
    }

    /**
     * @brief 返回队尾元素
     * 
     * @param value 
     * @return true 
     * @return false 
     */
    bool Back(T &value)
    {
        this->mutex_.Lock();
        if (this->size_ == 0)
        {
            this->mutex_.Unlock();
            return false;
        }
        value = this->array_[this->back_];
        this->mutex_.Unlock();
    }

    int Size()
    {
        int tmp = 0;

        this->mutex_.Lock();
        tmp = this->size_;
        this->mutex_.Unlock();
        return tmp;
    }

    int MaxSize()
    {
        int tmp = 0;
        this->mutex_.Lock();
        tmp = this->max_size_;
        this->mutex_.Unlock();
        return tmp;
    }

    /**
     * @brief 往队列添加元素，需要将所有使用队列的线程先唤醒
     *        当有元素push进队列,相当于生产者生产了一个元素
     *        若当前没有线程等待条件变量,则唤醒无意义
     * 
     * @param item 
     * @return true 
     * @return false 
     */
    bool Push(const T &item)
    {
        this->mutex_.Lock();
        if (this->size_ >= this->max_size_)
        {
            this->cond_.Broadcast();
            this->mutex_.Unlock();
            return false;
        }

        this->back_ = (this->back_ + 1) % this->max_size_;
        this->array_[this->back_] = item;
        this->size_++;

        this->cond_.Broadcast();
        this->mutex_.Unlock();
        return true;
    }

    /**
     * @brief pop时,如果当前队列没有元素,将会等待条件变量
     * 
     * @param item 
     * @return true 
     * @return false 
     */
    bool Pop(T &item)
    {
        this->mutex_.Lock();
        while (this->size_ <= 0)
        {
            if (!this->cond_.Wait(this->mutex_.get()))
            {
                this->mutex_.Unlock();
                return false;
            }
            
        }
        
        this->front_ = (this->front_ + 1) % this->max_size_;
        item = this->array_[this->front_];
        this->size_--;
        this->mutex_.Unlock();
        return true;
    }

    bool Pop(T &item, int ms_timeout)
    {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL);
        this->mutex_.Lock();
        if (this->size_ <= 0)
        {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if (!this->cond_.TimeWait(this->mutex_.get(), t))
            {
                this->mutex_.Unlock();
                return false;
            }
        }

        if (this->size_ <= 0)
        {
            this->mutex_.Unlock();
            return false;
        }

        this->front_ = (this->front_ + 1) % this->max_size_;
        item = this->array_[this->front_];
        this->size_--;
        this->mutex_.Unlock();
        return false;
    }
};


#endif // TINY_WEBSERVER_LOG_BLOCK_QUEUE_H