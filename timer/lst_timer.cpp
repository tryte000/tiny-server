#include "lst_timer.h"
#include "../http/http_conn.h"

SortTimerLst::SortTimerLst()
{
    this->head_ = NULL;
    this->tail_ = NULL;
}

SortTimerLst::~SortTimerLst()
{
    UtilTimer *tmp = this->head_;
    while (tmp)
    {
        this->head_ = tmp->next_;
        delete tmp;
        tmp = this->head_;
    }
}

void SortTimerLst::AddTimer(UtilTimer *timer)
{
    if (!timer)
    {
        return;
    }
    if (!this->head_)
    {
        this->head_ = this->tail_ = timer;
        return;
    }
    if (timer->expire_ < this->head_->expire_)
    {
        timer->next_ = this->head_;
        this->head_->prev_ = timer;
        this->head_ = timer;
        return;
    }
    this->AddTimer(timer, this->head_);
}


void SortTimerLst::AdjustTimer(UtilTimer *timer)
{
    if (!timer)
    {
        return;
    }
    UtilTimer *tmp = timer->next_;
    if (!tmp || (timer->expire_ < tmp->expire_))
    {
        return;
    }
    if (timer == this->head_)
    {
        this->head_ = this->head_->next_;
        this->head_->prev_ = NULL;
        timer->next_ = NULL;
        this->AddTimer(timer, this->head_);
    }
    else
    {
        timer->prev_->next_ = timer->next_;
        timer->next_->prev_ = timer->prev_;
        this->AddTimer(timer, timer->next_);
    }
}

void SortTimerLst::DelTimer(UtilTimer *timer)
{
    if (!timer)
    {
        return;
    }
    if ((timer == this->head_) && (timer == this->tail_))
    {
        delete timer;
        this->head_ = NULL;
        this->tail_ = NULL;
        return;
    }
    if (timer == this->head_)
    {
        this->head_ = this->head_->next_;
        this->head_->prev_ = NULL;
        delete timer;
        return;
    }
    if (timer == this->tail_)
    {
        this->tail_ = this->tail_->prev_;
        this->tail_->next_ = NULL;
        delete timer;
        return;
    }
    timer->prev_->next_ = timer->next_;
    timer->next_->prev_ = timer->prev_;
    delete timer;
}

void SortTimerLst::Tick()
{
    if (!this->head_)
    {
        return;
    }
    
    time_t cur = time(NULL);
    UtilTimer *tmp = this->head_;
    while (tmp)
    {
        if (cur < tmp->expire_)
        {
            break;
        }
        tmp->cb_func_(tmp->user_data_);
        this->head_ = tmp->next_;
        if (this->head_)
        {
            this->head_->prev_ = NULL;
        }
        delete tmp;
        tmp = this->head_;
    }
}

void SortTimerLst::AddTimer(UtilTimer *timer, UtilTimer *lst_head)
{
    UtilTimer *prev = lst_head;
    UtilTimer *tmp = prev->next_;
    while (tmp)
    {
        if (timer->expire_ < tmp->expire_)
        {
            prev->next_ = timer;
            timer->next_ = tmp;
            tmp->prev_ = timer;
            timer->prev_ = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next_;
    }
    if (!tmp)
    {
        prev->next_ = timer;
        timer->prev_ = prev;
        timer->next_ = NULL;
        this->tail_ = timer;
    }
}

void Utils::init(int timeslot)
{
    this->time_slot_ = timeslot;
}

//对文件描述符设置非阻塞
int Utils::SetNonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::Addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
    {
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    }
    else
    {
        event.events = EPOLLIN | EPOLLRDHUP;
    }

    if (one_shot)
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    this->SetNonblocking(fd);
}

//信号处理函数
void Utils::SigHandler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(Utils::pipefd_[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

//设置信号函数
void Utils::AddSig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::TimerHandler()
{
    this->timer_lst_.Tick();
    alarm(this->time_slot_);
}

void Utils::ShowError(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::pipefd_ = 0;
int Utils::epoolfd_ = 0;

// TODO 注释后会变怎么样
// class Utils;
void cb_func(ClientData *user_data)
{
    epoll_ctl(Utils::epoolfd_, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    HttpConn::user_count_--;
}