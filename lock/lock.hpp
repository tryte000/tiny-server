#ifndef TINY_WEBSERVER_LOCK_LOCK_H
#define TINY_WEBSERVER_LOCK_LOCK_H

#include <exception>
#include <semaphore.h>
#include <pthread.h>

class Sem
{
private:
    sem_t sem_;
    
public:
    Sem()
    {
        if (sem_init(&this->sem_, 0, 0) != 0)
        {
            throw std::exception();
        }
    }

    Sem(int num)
    {
        if (sem_init(&this->sem_, 0, num) != 0)
        {
            throw std::exception();
        }
    }

    ~Sem()
    {
        sem_destroy(&this->sem_);
    }

    bool Wait()
    {
        return sem_wait(&this->sem_) == 0;
    }

    bool Post()
    {
        return sem_post(&this->sem_) == 0;
    }
};


class Locker
{
public:
    Locker()
    {
        if (pthread_mutex_init(&this->mutex_, NULL) != 0)
        {
            throw std::exception();
        }
    }

    ~Locker()
    {
        pthread_mutex_destroy(&this->mutex_);
    }

    bool Lock()
    {
        return pthread_mutex_lock(&this->mutex_);
    }

    bool Unlock()
    {
        return pthread_mutex_unlock(&this->mutex_);
    }

    pthread_mutex_t *get()
    {
        return &this->mutex_;
    }

private:
    pthread_mutex_t mutex_;
};

class Cond
{
public:
    Cond()
    {
        if (pthread_cond_init(&this->cond_, NULL) != 0)
        {
            throw std::exception();
        }
    }

    ~Cond()
    {
        pthread_cond_destroy(&this->cond_);
    }

    bool Wait(pthread_mutex_t *mutex)
    {
        int ret = 0;
        ret = pthread_cond_wait(&this->cond_, mutex);
        return ret == 0;
    }

    bool TimeWait(pthread_mutex_t *mutex, struct timespec t)
    {
        int ret = 0;
        ret = pthread_cond_timedwait(&this->cond_, mutex, &t);
        return ret == 0;
    }

    bool Signal()
    {
        return pthread_cond_signal(&this->cond_) == 0;
    }

    bool Broadcast()
    {
        return pthread_cond_broadcast(&this->cond_) == 0;
    }

private:
    pthread_cond_t cond_;
};
#endif