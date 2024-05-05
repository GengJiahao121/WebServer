#ifndef CONFIG_H
#define CONFIG_H

#include "../webserver/webserver.h"

class Config
{
public:
    Config();
    ~Config(){};

    void parse_arg(int argc, char*argv[]);
public:

    //端口号
    int PORT;

    //日志写入方式
    int LOGWrite;

    //数据库连接池数量
    int sql_num;

    //线程池内的线程数量
    int thread_num;

    //是否关闭日志
    int close_log;
};

#endif