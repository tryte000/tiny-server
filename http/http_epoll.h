#ifndef TINY_WEBSERVER_HTTP_HTTP_EPOLL_H
#define TINY_WEBSERVER_HTTP_HTTP_EPOLL_H

/**
 * @brief 对文件描述符设置非阻塞
 * 
 * @param fd 
 * @return int 
 */
int setnonblockint(int fd);

/**
 * @brief 将内核事件表注册读事件，ET模式，选择开启 EPOLLONESHOT
 * 
 * @param epollfd 
 * @param fd 
 * @param one_shot 
 * @param trig_mode 
 */
void addfd(int epollfd, int fd, bool one_shot, int trig_mode);

/**
 * @brief 从内核时间表删除描述符
 * 
 * @param epollfd 
 * @param fd 
 */
void removefd(int epollfd, int fd);

/**
 * @brief 将事件重置为EPOLLONESHOT
 * 
 * @param epollfd 
 * @param fd 
 * @param ev 
 * @param trig_mode 
 */
void modfiyfd(int epollfd, int fd, int ev, int trig_mode);

#endif // TINY_WEBSERVER_HTTP_HTTP_EPOLL_H