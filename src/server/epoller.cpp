

#include "epoller.h"

/// @maxEvent 决定epoller的最大监听事件，默认为1024
Epoller::Epoller(int maxEvent) : epollFd_(epoll_create(512)),
                                 events_(maxEvent)
{
    assert(epollFd_ >= 0 && events_.size() > 0);
}

Epoller::~Epoller() {
    close(epollFd_);
}
/// @brief 添加一个文件描述符到epoll
/// @fd 文件描述符，例如socket
/// @events 事件类型
bool Epoller::AddFd(int fd, uint32_t events) {
    if (fd < 0) {
        return false;
    }
    /// @ev 将当前事件类型，文件描述符存入实例化对象ev
    struct epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events;
    /// @epoll_ctl 用于控制 epoll 实例的系统调用 若成功添加事件到epoll实例则返回0
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev);
}
/// @brief 修改一个已经添加到epoll中的文件描述符所监听的事件类型
bool Epoller::ModFd(int fd, uint32_t events) {
    if (fd < 0) {
        return false;
    }

    struct epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events;
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev);
}
/// @brief 从epoll中删除一个文件描述符
bool Epoller::DelFd(int fd) {
    if (fd < 0) {
        return false;
    }
    struct epoll_event ev = { 0 };
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
}
///@brief 设置epoll的阻塞模式
///@timeout 为-1表示永久返回，0表示非阻塞模式，立刻返回
int Epoller::Wait(int timeout) {
    return epoll_wait(epollFd_, &events_[0], static_cast<int>(events_.size()), timeout);
}

///@brief 用于获取存储在事件数组中的指定位置的文件描述符
int Epoller::GetEventFd(size_t i) const {
    assert(i < events_.size() && i >= 0);
    return events_[i].data.fd;
}
///@brief 用于获取存储在事件数组中的指定位置的事件类型
uint32_t Epoller::GetEvent(size_t i) const {
    assert(i < events_.size() && i >= 0);
    return events_[i].events;
}