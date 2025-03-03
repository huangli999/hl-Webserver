/*
    RAII机制 管理从数据库连接池获得的连接句柄
*/

#ifndef SQLCONNRAII_H
#define SQLCONNRAII_H

#include "sqlconnpool.h"
//RAII 是一种常见的 C++ 编程技巧，资源（如数据库连接、文件句柄、内存等）的获取和释放与对象的生命周期绑定
//封装了数据库连接的生命周期管理，确保连接池中的连接在使用完后被正确归还，避免了连接泄漏，并简化了数据库连接的管理
//否则需要显式的释放，可能出线连接泄露的风险，浪费资源
class SqlConnRAII {
public:
    SqlConnRAII(SqlConnPool* connpool): sql_h_(nullptr) {
        assert(connpool);
        if (connpool) {
            sql_h_ = connpool->GetConn();
            connpool_ = connpool;
        }
    }

    ~SqlConnRAII() {
        if (sql_h_) {
            connpool_->FreeConn(sql_h_);
        }
    }
    
    bool HasPtr() {
        return sql_h_ != nullptr;
    }

    MYSQL* GetPtr() {
        return sql_h_;
    }


private:
    MYSQL* sql_h_;
    SqlConnPool* connpool_;

};


#endif
