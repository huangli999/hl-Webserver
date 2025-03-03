/*
    异步日志系统
*/

#ifndef LOG_H
#define LOG_H

#include <mutex>
#include <thread>
#include <sys/time.h>
#include <string>
#include <memory>
#include <sys/stat.h>  // mkdir
#include <stdarg.h>    // vastart va_end
#include "../buffer/buffer.h"
#include "blockqueue.hpp"

class Log {
public:
    /// @brief 初始化函数
    /// @param level 写水平(debug info warn error)
    /// @param path 保存路径
    /// @param suffix 文件后缀
    /// @param maxQueueCapacity 队列最大容量
    void init(int level = 1, const char* path = "../logFile", const char* suffix = ".log", int maxQueueCapacity = 1024);
    
    // 单例模式
    /// @brief 获取单例指针
    /// @return Log指针
    static Log* GetInstance() {
        static Log inst;
        return &inst;
    }
    
    /// @brief 写线程绑定的函数
    static void FlushLogThread();

    /// @brief 类似 printf 的可变参数形式写入函数
    /// @param level 写入水平
    /// @param format 字符串格式
    /// @param  可变参数
    void write(int level, const char* format, ...);
    
    /// @brief 刷盘函数, 刷新日志缓冲区中的数据，将其写入文件
    void flush();
    /// @brief 把缓冲队列中的数据全部刷盘
    void flushAll();


    /// @brief 获取当前的写水平
    /// @return 写水平
    int GetLevel();
    /// @brief 设置当前的写水平
    /// @param level 写水平
    void Setlevel(int level);
    
    bool IsOpen() {
        return isOpen_;
    }

private:
    /// @brief 构造函数
    Log();
    /// @brief 确定对应的写入水平
    /// @param level 0-debug, 1-info, 2-warn, 3-error, default-info
    void AppendLogLevelTitle_(int level);
    /// @brief 析构函数
    virtual ~Log();
    /// @brief 异步写函数
    void AsyncWrite_();

private:
    static const int LOG_PATH_LEN = 256;
    static const int LOG_NAME_LEN = 256;
    static const int MAX_LINES = 50000;

    const char* path_;//日志文件存储的路径
    const char* suffix_;//日志文件的后缀

    int lineCount_;//记录当前日志文件中的行数
    int toDay_;//记录当前日期

    bool isOpen_;

    Buffer buff_;//日志缓冲区
    int level_;
    bool isAsync_;//是否使用异步日志写入

    FILE* fp_;//日志文件的文件指针，用于写入日志
    std::unique_ptr<BlockQueue<std::string>> deque_;//日志缓冲队列，存储待写入的日志内容
    std::unique_ptr<std::thread> writeThread_;//异步写日志的后台线程
    std::mutex mtx_;
};

#define LOG_BASE(level, format, ...) \
    do {\
        Log* log = Log::GetInstance();\
        if (log->IsOpen() && log->GetLevel() <= level) {\
            log->write(level, format, ##__VA_ARGS__); \
            log->flush(); \
        }\
    } while(0);

#define LOG_DEBUG(format, ...) do {LOG_BASE(0, format, ##__VA_ARGS__)} while(0);
#define LOG_INFO(format, ...) do {LOG_BASE(1, format, ##__VA_ARGS__)} while(0);
#define LOG_WARN(format, ...) do {LOG_BASE(2, format, ##__VA_ARGS__)} while(0);
#define LOG_ERROR(format, ...) do {LOG_BASE(3, format, ##__VA_ARGS__)} while(0);



#endif