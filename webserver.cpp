#include "webserver.h"

WebServer::WebServer()
{
    // HttpConn 类对象
    this->users_ = new HttpConn[MAX_FD];

    // root 文件夹路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    this->root_ = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(this->root_, server_path);
    strcat(this->root_, root);

    // 定时器
    this->users_timer_ = new ClientData[MAX_FD];
}

WebServer::~WebServer()
{
    close(this->epollfd_);
    close(this->listenfd_);
    close(this->pipefd_[1]);
    close(this->pipefd_[0]);
    delete[] this->users_;
    delete[] this->users_timer_;
    delete this->pool_;
}

void WebServer::Init(int port, string user, string passWord,
    string databaseName, int log_write, int opt_linger, int trigmode,
    int sql_num, int thread_num, int close_log, int actor_model)
{
    this->port_ = port;
    this->user_ = user;
    this->password_ = passWord;
    this->database_name_ = databaseName;
    this->sql_num_ = sql_num;
    this->thread_num_ = thread_num;
    this->log_write_ = log_write;
    this->opt_linger_ = opt_linger;
    this->trigmode_ = trigmode;
    this->close_log_ = close_log;
    this->actormodel_ = actor_model;
}

void WebServer::TrigMode()
{
    // LT + LT
    if (0 == this->trigmode_)
    {
        this->listen_trigmode_ = 0;
        this->conn_trigmode_ = 0;
    }
    // LT + ET
    else if (1 == this->trigmode_)
    {
        this->listen_trigmode_ = 0;
        this->conn_trigmode_ = 1;
    }
    // ET + LT
    else if (2 == this->trigmode_)
    {
        this->listen_trigmode_ = 1;
        this->conn_trigmode_ = 0;
    }
    // ET + ET
    else if (3 == this->trigmode_)
    {
        this->listen_trigmode_ = 1;
        this->conn_trigmode_ = 1;
    }
}

void WebServer::LogWrite()
{
    if (0 == this->close_log_)
    {
        // 初始化日志
        if (1 == this->log_write_)
        {
            Log::GetInstance()->Init("./ServerLog", this->close_log_, 2000, 800000, 800);
        }
        else
            Log::GetInstance()->Init("./ServerLog", this->close_log_, 2000, 800000, 0);
    }
}

void WebServer::SqlPool()
{
    // 初始化数据库连接池
    this->conn_pool_ = SqlConnectionPool::GetInstance();
    this->conn_pool_->init("localhost", this->user_, this->password_,
        this->database_name_, 3306, this->sql_num_, this->close_log_);

    // 初始化数据库读取表
    this->users_->InitMysqlResult(this->conn_pool_);
}

void WebServer::CreateThreadPool()
{
    // 线程池
    this->pool_ = new ThreadPool<HttpConn>(this->actormodel_, this->conn_pool_,
        this->thread_num_);
}

void WebServer::EventListen()
{
    // 网络编程基础步骤
    this->listenfd_ = socket(PF_INET, SOCK_STREAM, 0);
    assert(this->listenfd_ >= 0);

    // 优雅关闭连接
    if (0 == this->opt_linger_)
    {
        struct linger tmp = {0, 1};
        setsockopt(this->listenfd_, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == this->opt_linger_)
    {
        struct linger tmp = {1, 1};
        setsockopt(this->listenfd_, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(this->port_);

    int flag = 1;
    setsockopt(this->listenfd_, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(this->listenfd_, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(this->listen_trigmode_, 5);
    assert(ret >= 0);

    this->utils_.init(TIMESLOT);

    // epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    this->epollfd_ = epoll_create(5);
    assert(this->epollfd_ != -1);

    this->utils_.Addfd(this->epollfd_, this->listenfd_, false,
        this->listen_trigmode_);
    HttpConn::epollfd_ = this->epollfd_;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, this->pipefd_);
    assert(ret != -1);
    this->utils_.SetNonblocking(this->pipefd_[1]);
    this->utils_.Addfd(this->epollfd_, this->pipefd_[0], false, 0);

    this->utils_.AddSig(SIGPIPE, SIG_IGN);
    this->utils_.AddSig(SIGALRM, this->utils_.SigHandler, false);
    this->utils_.AddSig(SIGTERM, this->utils_.SigHandler, false);

    alarm(TIMESLOT);

    // 工具类,信号和描述符基础操作
    Utils::pipefd_ = this->pipefd_;
    Utils::epoolfd_ = this->epollfd_;
}

void WebServer::Timer(int connfd, struct sockaddr_in client_address)
{
    this->users_[connfd].Init(connfd, client_address, this->root_,
        this->conn_trigmode_, this->close_log_, this->user_, this->password_,
        this->database_name_);

    // 初始化client_data数据
    // 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    this->users_timer_[connfd].address = client_address;
    this->users_timer_[connfd].sockfd = connfd;
    UtilTimer *timer = new UtilTimer;
    timer->user_data_ = &this->users_timer_[connfd];
    timer->cb_func_ = cb_func;
    time_t cur = time(NULL);
    timer->expire_ = cur + 3 * TIMESLOT;
    this->users_timer_[connfd].timer = timer;
    this->utils_.timer_lst_.AddTimer(timer);
}

/**
 * @brief 若有数据传输，则将定时器往后延迟3个单位
 *        并对新的定时器在链表上的位置进行调整
 * 
 * @param timer 
 */
void WebServer::AdjustTimer(UtilTimer *timer)
{
    time_t cur = time(NULL);
    timer->expire_ = cur + 3 * TIMESLOT;
    this->utils_.timer_lst_.AdjustTimer(timer);

    LOG_INFO("%s", "adjust timer once");
}

void WebServer::DealTimer(UtilTimer *timer, int sockfd)
{
    timer->cb_func_(&this->users_timer_[sockfd]);
    if (timer)
    {
        this->utils_.timer_lst_.DelTimer(timer);
    }

    LOG_INFO("close fd %d", this->users_timer_[sockfd].sockfd);
}

bool WebServer::Dealclinetdata()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    if (0 == this->listen_trigmode_)
    {
        int connfd = accept(this->listenfd_, (struct sockaddr *)&client_address,
            &client_addrlength);
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (HttpConn::user_count_ >= MAX_FD)
        {
            this->utils_.ShowError(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        this->Timer(connfd, client_address);
    }

    else
    {
        while (1)
        {
            int connfd = accept(this->listenfd_,
                (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (HttpConn::user_count_ >= MAX_FD)
            {
                this->utils_.ShowError(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            this->Timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

bool WebServer::Dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(this->pipefd_[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                timeout = true;
                break;
            }
            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

void WebServer::Dealwithread(int sockfd)
{
    UtilTimer *timer = this->users_timer_[sockfd].timer;

    //reactor
    if (1 == this->actormodel_)
    {
        if (timer)
        {
            this->AdjustTimer(timer);
        }

        //若监测到读事件，将该事件放入请求队列
        this->pool_->Append(this->users_ + sockfd, 0);

        while (true)
        {
            if (1 == this->users_[sockfd].improv_)
            {
                if (1 == this->users_[sockfd].timer_flag_)
                {
                    this->DealTimer(timer, sockfd);
                    this->users_[sockfd].timer_flag_ = 0;
                }
                this->users_[sockfd].improv_ = 0;
                break;
            }
        }
    }
    else
    {
        // proactor
        if (this->users_[sockfd].ReadOnce())
        {
            LOG_INFO("deal with the client(%s)",
                inet_ntoa(this->users_[sockfd].GetAddress()->sin_addr));

            // 若监测到读事件，将该事件放入请求队列
            this->pool_->AppendP(this->users_ + sockfd);

            if (timer)
            {
                this->AdjustTimer(timer);
            }
        }
        else
        {
            this->DealTimer(timer, sockfd);
        }
    }
}

void WebServer::Dealwithwrite(int sockfd)
{
    UtilTimer *timer = this->users_timer_[sockfd].timer;
    // reactor
    if (1 == this->actormodel_)
    {
        if (timer)
        {
            this->AdjustTimer(timer);
        }

        this->pool_->Append(this->users_ + sockfd, 1);

        while (true)
        {
            if (1 == this->users_[sockfd].improv_)
            {
                if (1 == this->users_[sockfd].timer_flag_)
                {
                    this->DealTimer(timer, sockfd);
                    this->users_[sockfd].timer_flag_ = 0;
                }
                this->users_[sockfd].improv_ = 0;
                break;
            }
        }
    }
    else
    {
        // proactor
        if (this->users_[sockfd].Write())
        {
            LOG_INFO("send data to the client(%s)",
                inet_ntoa(this->users_[sockfd].GetAddress()->sin_addr));

            if (timer)
            {
                this->AdjustTimer(timer);
            }
        }
        else
        {
            this->DealTimer(timer, sockfd);
        }
    }
}

void WebServer::EventLoop()
{
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server)
    {
        int number = epoll_wait(this->epollfd_, this->events_, MAX_EVENT_NUMBER,
            -1);
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = this->events_[i].data.fd;

            // 处理新到的客户连接
            if (sockfd == this->listenfd_)
            {
                bool flag = this->Dealclinetdata();
                if (false == flag)
                {
                    continue;
                }
            }
            else if (this->events_[i].events &
                (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 服务器端关闭连接，移除对应的定时器
                UtilTimer *timer = this->users_timer_[sockfd].timer;
                this->DealTimer(timer, sockfd);
            }
            // 处理信号
            else if ((sockfd == this->pipefd_[0]) &&
                (this->events_[i].events & EPOLLIN))
            {
                bool flag = this->Dealwithsignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            // 处理客户连接上接收到的数据
            else if (this->events_[i].events & EPOLLIN)
            {
                this->Dealwithread(sockfd);
            }
            else if (this->events_[i].events & EPOLLOUT)
            {
                this->Dealwithwrite(sockfd);
            }
        }
        if (timeout)
        {
            this->utils_.TimerHandler();

            LOG_INFO("%s", "timer tick");

            timeout = false;
        }
    }
}