#include "webserver.h"
#include "../pool/ThreadPool.hpp"
///@port 端口号
///@trigMode 触发模式
///@timeoutMS 超时时间
///@sqlPort 数据库端口号
///@sqlUser 数据库用户名
///@sqlPwd 数据库密码
///@dbName 数据库名字
///@sqlPoolNum 数据库连接池
///@threadNum 线程池
///@MaxEvent 最大事件数
WebServer::WebServer(int port, int trigMode, int timeoutMS, bool OpenLinger,
              int sqlPort, const char* sqlUser, const char* sqlPwd,
              const char* dbName, int sqlPoolNum, int threadNum,
              int MaxEvent) :
              port_(port), openLinger_(OpenLinger), timeoutMS_(timeoutMS), isClose_(false),
              timeWheel_(new TimeWheel()), threadpool_(new ThreadPool(threadNum)), epoller_(new Epoller(MaxEvent))
{
    ///@brief 获取当前的工作目录路径，并返回指向该路径的指针
    ///@256 缓冲区大小
    srcDir_ = getcwd(nullptr, 256);
    assert(srcDir_);


    ///@brief 将将 "/../resources/" 字符串追加到当前目录路径后，构造出静态资源目录的路径。
    // strncat 追加时限制了追加的字符数为 16，确保不会越界。
    strncat(srcDir_, "/../resources/", 16);


    ///@srcDir  HTTP 连接设置资源目录
    ///@userCount 连接用户数
    HttpConn::srcDir = srcDir_;
    HttpConn::userCount = 0;


    //初始化数据库连接池
    SqlConnPool::GetInstance()->Init("localhost", sqlPort, sqlUser, sqlPwd, dbName, sqlPoolNum);
    
    
    //初始化事件模式
    InitEventMode_(trigMode);
    if (!InitSocket_()) {
        isClose_ = true;
    }
    if (isClose_) {
        LOG(INFO) << "========================= Server init error! =======================";
    }
    else {
        LOG(INFO) << "========================= Server init! =============================";
        LOG(INFO) << "Port: " << port_ << ", OpenLinger: " << (openLinger_ ? "true" : "false");
        LOG(INFO) << "Listen Mode: "<< (listenEvent_ & EPOLLET ? "ET" : "LT") 
                  << ", OpenConn Mode: " << (connEvent_ & EPOLLET ? "ET" : "LT");
        LOG(INFO) << "srcDir: " << HttpConn::srcDir;
        LOG(INFO) << "SqlConnPool num: " << sqlPoolNum << ", ThreadPool num: " <<  threadNum;
    }
}

WebServer::~WebServer() {
    close(listenFd_);
    isClose_ = true;
    free(srcDir_);
    SqlConnPool::GetInstance()->ClosePool();
    timeWheel_->Close();
}

///@brief 负责启动服务器并处理事件
void WebServer::Start() {
    int timeMS = -1; // epoll_wait timeout == -1 表示没有事件发生就阻塞
    if (!isClose_) {
        LOG(INFO) << "========================= Server Start! =======================";
        // 开启定时器
        timeWheel_->Run();
    }
    while (!isClose_) {
        int eventCnt = epoller_->Wait(timeMS); // 阻塞等待下一个事件发生
        for (int i = 0; i < eventCnt; ++i) {
            /*  处理事件 */
            int fd = epoller_->GetEventFd(i);
            uint32_t events = epoller_->GetEvent(i);
            if (fd == listenFd_) {
                DealListen_();
            }
            else if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                assert(users_.count(fd) > 0);
                timeWheel_->RemoveTask(users_[fd]->GetTimeOutKey());
                CloseConn_(users_[fd]);
            }
            else if (events & EPOLLIN) {
                assert(users_.count(fd) > 0);
                DealRead_(users_[fd]);
            }
            else if (events & EPOLLOUT) {
                assert(users_.count(fd) > 0);
                DealWrite_(users_[fd]);
            }
            else {
                LOG(ERROR) << "Unexpected event";
            }
        }
    }
}



bool WebServer::InitSocket_() {
    int ret;
    struct sockaddr_in addr;
    if (port_ > 65535 || port_ < 1024) {
        LOG(ERROR) << "Port: " << port_ << " error!";
        return false;
    }


    //初始化addr结构体指向的地址
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;//表示ipv4
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // 监听所有的网卡地址
    addr.sin_port = htons(port_);//设置监听的端口号，htons 用于将端口号从主机字节序转换为网络字节序。

    struct linger optLinger;
    memset(&optLinger, 0, sizeof(optLinger));
    if (openLinger_) {
        /* 优雅关闭: 直到所剩的数据发送完毕或超时 */
        optLinger.l_onoff = 1;
        optLinger.l_linger = 1; // 超时时间
    }
    

    ///@SOCK_STREAM 流式套接字即tcp
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        LOG(ERROR) << "Create socket error!";
        return false;
    }

    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if (ret < 0) {
        close(listenFd_);
        LOG(ERROR) << "Init linget error!";
        return false;
    }

    // 端口号复用
    // 只有最后一个 socket 会正常接收数据
    int optval = 1;
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
    if (ret < 0) {
        close(listenFd_);
        LOG(ERROR) << "Set socket setsocketopt error!";
        return false;
    }

    //bind 将刚刚创建的套接字绑定到指定的地址和端口上
    ret = bind(listenFd_, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0) {
        close(listenFd_);
        LOG(ERROR) << "Bind error!";
        return false;
    }

    //监听端口
    ret = listen(listenFd_, 6);
    if (ret < 0) {
        close(listenFd_);
        LOG(ERROR) << "Listen error!";
        return false;
    }

    //将监听套接字添加到 epoll 实例中
    ret = epoller_->AddFd(listenFd_, listenEvent_ | EPOLLIN);
    if (ret == 0) {
        close(listenFd_);
        LOG(ERROR) << "Add listenfd error!";
        return false;
    }
    
    SetFdNonBlock(listenFd_); // 设置 listenFd_ 是非阻塞的
    return true;
}

int WebServer::SetFdNonBlock(int fd) {
    assert(fd > 0);
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
}

void WebServer::InitEventMode_(int trigMode) {
    /*
        EPOLLRDHUB:
        在内核2.6.17（不含）以前版本，
        要想知道对端是否关闭socket，上层应用只能通过调用recv来进行判断，
        在2.6.17以后，这种场景上层只需要判断EPOLLRDHUP即可，无需在调用recv这个系统调用。
    */
    listenEvent_ = EPOLLRDHUP; // 当客户端关闭连接时, 内核直接关闭, 不用再判断recv读取为0
    /*
        EPOLLONESHOT:
            为关联的文件描述符设置一次性行为。
            这意味着在使用 epoll_wait(2) 拉出事件后，关联的文件描述符在内部被禁用，
            并且 epoll 接口不会报告其他事件。
            用户必须使用 EPOLL_CTL_MOD 调用 epoll_ctl() 以使用新的事件掩码重新装备文件描述符。
    */
    
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP;
    //EPOLLET 边缘触发，只有文件描述符发生变化触发，并且必须马上处理，否则可能丢失
    switch (trigMode) {
        case 0:
            break;
        case 1:
            connEvent_ |= EPOLLET;
            break;
        case 2:
            listenEvent_ |= EPOLLET;
            break;
        case 3:
            connEvent_ |= EPOLLET;
            listenEvent_ |= EPOLLET;
            break;
        default:
            connEvent_ |= EPOLLET;
            listenEvent_ |= EPOLLET;
            break;
    }
    HttpConn::isET = (connEvent_ & EPOLLET);
}


void WebServer::SendError_(int fd, const char* info) {
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);
    if (ret < 0) {
        LOG(WARNING) << "Send error to client [" << fd << "] error!";
    }
    close(fd);
}

void WebServer::CloseConn_(connPtr client) {
    assert(client);
    LOG(INFO) << "Client[" << client->GetFd() << "] quit!";
    epoller_->DelFd(client->GetFd());

    client->Close();
    
    int fd = client->GetFd();
    //确保线程安全
    std::lock_guard<std::mutex> lk(users_lock_);
    users_.erase(fd); // ***** 这里要把 fd 对应地从 users_ 删除 *****
}

//这个函数的作用是延长某个客户端的连接超时时间
void WebServer::ExtentTime_(connPtr client) {
    assert(client);
    if (timeoutMS_ > 0) {
        timeWheel_->Addtask(client->GetTimeOutKey(), timeoutMS_, &WebServer::CloseConn_, this, client);
    }
}

//添加一个新的客户端连接
void WebServer::AddClient_(int fd, sockaddr_in addr) {
    assert(fd > 0);

    //使用make_shared是为资源共享（多个线程可以安全的共享),避免内存泄漏，引用计数为0时，释放内存
    connPtr hc = std::make_shared<HttpConn>();//创建一个新的客户端连接对象
    hc->Init(fd, addr);

    if (timeoutMS_ > 0) {
        timeWheel_->Addtask(hc->GetTimeOutKey(), timeoutMS_, &WebServer::CloseConn_, this, hc);
    }

    epoller_->AddFd(fd, EPOLLIN | connEvent_);
    SetFdNonBlock(fd);
    LOG(INFO) << "Client[" << hc->GetFd() << "] connected!";

    // 将客户端添加到 users_ 容器中，便于后续操作
    std::lock_guard<std::mutex> lk(users_lock_);
    users_[fd] = hc;
}

void WebServer::DealListen_() {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    do {
        int fd = accept(listenFd_, (struct sockaddr*)&addr, &len);
        if (fd <= 0) {
            return;
        }
        else if (HttpConn::userCount >= MAX_FD) {
            SendError_(fd, "Server busy!, ");
            LOG(WARNING) << "Client is full!";
            return;
        }
        AddClient_(fd, addr);
    } while (listenEvent_ & EPOLLET); //保证读完
}
//处理客户端的读取事件，并将处理任务交给线程池
void WebServer::DealRead_(connPtr client) {
    assert(client);
    ExtentTime_(client); // 延长时间
    // threadpool_->AddTask(std::bind(&WebServer::OnRead_, this, client)); // 线程池处理
    threadpool_->enqueue(&WebServer::OnRead_, this, client); // 线程池处理
}

void WebServer::DealWrite_(connPtr client) {
    assert(client);
    ExtentTime_(client); // 延长时间
    // threadpool_->AddTask(std::bind(&WebServer::OnWrite_, this, client)); // 线程池处理
    threadpool_->enqueue(&WebServer::OnWrite_, this, client); // 线程池处理
}

void WebServer::OnRead_(connPtr client) {
    assert(client);
    int ret = -1;
    int readErrno = 0;
    ret = client->Read(&readErrno);
    if (ret <= 0 && readErrno != EAGAIN) {
        timeWheel_->RemoveTask(client->GetTimeOutKey());
        CloseConn_(client);
        return;
    }
    OnProcess(client);
}

void WebServer::OnProcess(connPtr client) {
    if (client->Process()) {
        // 读数据并成功解析请求报文, 准备发送响应报文
        // 所以变成 EPOLLOUT 
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
    }
    else {
        // 写数据结束, 改为读 EPOLLIN
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
    }
}

void WebServer::OnWrite_(connPtr client) {
    assert(client);

    int ret = -1;
    int writeErrno = 0;
    ret = client->Write(&writeErrno);
    if (client->ToWriteBytes() == 0) {
        // 传输完成
        if (client->IsKeepAlive()) {
            OnProcess(client);
            return;
        }
    }
    else if(ret < 0) {
        if (writeErrno == EAGAIN) {
            // 继续传输
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
            return;
        }
    }
    // 其他情况
    timeWheel_->RemoveTask(client->GetTimeOutKey());
    CloseConn_(client);
}

