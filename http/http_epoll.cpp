#include "http_epoll.h"

#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>

/**
 * @brief 对文件描述符设置非阻塞
 * 
 * @param fd 
 * @return int 
 */
int setnonblockint(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

/**
 * @brief 将内核事件表注册读事件，ET模式，选择开启 EPOLLONESHOT
 * 
 * @param epollfd 
 * @param fd 
 * @param one_shot 
 * @param trig_mode 
 */
void addfd(int epollfd, int fd, bool one_shot, int trig_mode)
{
    epoll_event event;
    event.data.fd = fd;

    if (trig_mode == 0)
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
    setnonblockint(fd);
}

/**
 * @brief 从内核时间表删除描述符
 * 
 * @param epollfd 
 * @param fd 
 */
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

/**
 * @brief 将事件重置为EPOLLONESHOT
 * 
 * @param epollfd 
 * @param fd 
 * @param ev 
 * @param trig_mode 
 */
void modfiyfd(int epollfd, int fd, int ev, int trig_mode)
{
    epoll_event event;
    event.data.fd = fd;

    if (trig_mode == 0)
    {
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    }
    else
    {
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    }
    
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}