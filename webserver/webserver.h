#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>

#include "../http/http_conn.h"
#include "../threadpool/threadpool.h"


const int MAX_FD = 65536;           //最大文件描述符
const int MAX_EVENT_NUMBER = 10000; //最大事件数
const int TIMESLOT = 5;             //最小超时单位

class WebServer{
public:
    WebServer();
    ~WebServer();

public:
    // 方法：
    void init(int port , string user, string passWord, string databaseName,
              int log_write, int sql_num,
              int thread_num, int close_log, int epoll_et);

    // 日志系统
    void log_write();
    // 数据库连接池
    void sql_pool();
    // 线程池
    void thread_pool();
    // 事件监听
    void eventListen();
    void eventLoop();

    bool dealclientdata();
    void deal_timer(util_timer *timer, int sockfd);
    bool dealwithsignal(bool& timeout, bool& stop_server);
    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);

    // 定时器
    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(util_timer *timer);
    
public:

    //端口号
    int m_port;

    // 资源地址
    char *m_root;

    int m_pipefd[2];
    int m_epollfd;
    http_conn *users;

    // 日志
    int m_log_write;
    int m_close_log;

    // 数据库
    connection_pool *m_connPool;
    string m_user;         //登陆数据库用户名
    string m_passWord;     //登陆数据库密码
    string m_databaseName; //使用数据库名
    int m_sql_num;

    //线程池相关
    threadpool<http_conn> *m_pool;
    int m_thread_num;

    //epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];
    int m_epoll_et;

    int m_listenfd;


    //定时器相关
    client_data *users_timer;
    Utils utils;

};


#endif