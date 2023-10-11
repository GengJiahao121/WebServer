#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <pthread.h>
#include "lst_timer.h"

#define FD_LIMIT 65535
#define MAX_EVENT_NUMBER 1024
#define TIMESLOT 5

static int pipefd[2];   
static sort_timer_lst timer_lst;    // 定时器双端链表
static int epollfd = 0;

int setnonblocking( int fd )
{
    int old_option = fcntl( fd, F_GETFL );
    int new_option = old_option | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_option );
    return old_option;
}

// 向epoll监听的事件数组中添加fd
void addfd( int epollfd, int fd )
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;   // 监听的事件类型
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );    // 添加系统调用
    setnonblocking( fd );   // 将fd设置为非阻塞
}

void sig_handler( int sig )
{
    int save_errno = errno;
    int msg = sig;
    send( pipefd[1], ( char* )&msg, 1, 0 );     // 发送
    errno = save_errno;
}


// 当指定的信号发生时，执行sig_handler回调函数
void addsig( int sig )
{
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

void timer_handler()
{
    // 定时处理任务，实际上就是调用tick()函数
    timer_lst.tick();
    // 因为一次 alarm 调用只会引起一次SIGALARM 信号，所以我们要重新定时，以不断触发 SIGALARM信号。
    alarm(TIMESLOT);
}

// 定时器回调函数，它删除非活动连接socket上的注册事件，并关闭之。
void cb_func( client_data* user_data )
{
    epoll_ctl( epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0 );
    assert( user_data );
    close( user_data->sockfd );
    printf( "close fd %d\n", user_data->sockfd );
}

int main( int argc, char* argv[] ) {

    // 如果没有传入端口号，直接退出
    if( argc <= 1 ) {
        printf( "usage: %s port_number\n", basename( argv[0] ) );
        return 1;
    }

    // 字符串转换为整数
    int port = atoi( argv[1] );

    int ret = 0;
    struct sockaddr_in address;     // 定义一个socket地址
    bzero( &address, sizeof( address ) );   // 将sockaddr_in结构体中的所有变量初始化为0
    address.sin_family = AF_INET;   // 初始化协议族
    address.sin_addr.s_addr = INADDR_ANY;   // 绑定本机的ip地址
    address.sin_port = htons( port );   // 绑定端口号，主机字节序（host byte order）的端口号转换为网络字节序（network byte order）

    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );   // 创建
    assert( listenfd >= 0 );

    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );    // 绑定
    assert( ret != -1 );

    ret = listen( listenfd, 5 );    // 监听
    assert( ret != -1 );

    epoll_event events[ MAX_EVENT_NUMBER ];     // 定义epoll可以处理的最大事件数量
    int epollfd = epoll_create( 5 );    // 创建，使用epoll 实现I/O多路复用
    assert( epollfd != -1 );
    addfd( epollfd, listenfd );     

    // 创建管道
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert( ret != -1 );
    setnonblocking( pipefd[1] );    // 设置为非阻塞
    addfd( epollfd, pipefd[0] );    // 添加到epoll监听数组中，监听的事件为epollin，说明pipefd[0]为读端

    // 设置信号处理函数
    addsig( SIGALRM );  // 当发生SIGALRM信号时（超时），向管道中写数据
    addsig( SIGTERM );  // 程序正常结束时，向管道中写数据
    bool stop_server = false;

    client_data* users = new client_data[FD_LIMIT];  
    bool timeout = false;
    alarm(TIMESLOT);  // 定时,5秒后产生SIGALARM信号

    while( !stop_server )
    {
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        if ( ( number < 0 ) && ( errno != EINTR ) ) {
            printf( "epoll failure\n" );
            break;
        }
    
        for ( int i = 0; i < number; i++ ) {
            int sockfd = events[i].data.fd;
            if( sockfd == listenfd )    // 主线程，用来监听是否有客户端连接进来
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );   // 接受连接并返回一个新的socket套接字存储客户端信息
                addfd( epollfd, connfd );
                users[connfd].address = client_address;
                users[connfd].sockfd = connfd;
                
                // 创建定时器，设置其回调函数与超时时间，然后绑定定时器与用户数据，最后将定时器添加到链表timer_lst中
                util_timer* timer = new util_timer;
                timer->user_data = &users[connfd];
                timer->cb_func = cb_func;
                time_t cur = time( NULL );
                timer->expire = cur + 3 * TIMESLOT;
                users[connfd].timer = timer;
                timer_lst.add_timer( timer );
            } else if( ( sockfd == pipefd[0] ) && ( events[i].events & EPOLLIN ) ) {    // 有可读事件进来
                // 处理信号
                int sig;
                char signals[1024];
                ret = recv( pipefd[0], signals, sizeof( signals ), 0 );
                if( ret == -1 ) {
                    continue;
                } else if( ret == 0 ) {
                    continue;
                } else  {
                    for( int i = 0; i < ret; ++i ) {
                        switch( signals[i] )  {
                            case SIGALRM:
                            {
                                // 用timeout变量标记有定时任务需要处理，但不立即处理定时任务
                                // 这是因为定时任务的优先级不是很高，我们优先处理其他更重要的任务。
                                timeout = true;
                                break;
                            }
                            case SIGTERM:
                            {
                                stop_server = true;
                            }
                        }
                    }
                }
            }
            else if(  events[i].events & EPOLLIN )  // 客户端有新的数据到来可读
            {
                memset( users[sockfd].buf, '\0', BUFFER_SIZE );
                ret = recv( sockfd, users[sockfd].buf, BUFFER_SIZE-1, 0 );
                printf( "get %d bytes of client data %s from %d\n", ret, users[sockfd].buf, sockfd );
                util_timer* timer = users[sockfd].timer;
                if( ret < 0 )
                {
                    // 如果发生读错误，则关闭连接，并移除其对应的定时器
                    if( errno != EAGAIN )
                    {
                        cb_func( &users[sockfd] );
                        if( timer )
                        {
                            timer_lst.del_timer( timer );
                        }
                    }
                }
                else if( ret == 0 )
                {
                    // 如果对方已经关闭连接，则我们也关闭连接，并移除对应的定时器。
                    cb_func( &users[sockfd] );
                    if( timer )
                    {
                        timer_lst.del_timer( timer );
                    }
                }
                else
                {
                    // 如果某个客户端上有数据可读，则我们要调整该连接对应的定时器，以延迟该连接被关闭的时间。
                    if( timer ) {
                        time_t cur = time( NULL );
                        timer->expire = cur + 3 * TIMESLOT;
                        printf( "adjust timer once\n" );
                        timer_lst.adjust_timer( timer );
                    }
                }
            }
           
        }

        // 最后处理定时事件，因为I/O事件有更高的优先级。当然，这样做将导致定时任务不能精准的按照预定的时间执行。
        if( timeout ) {
            timer_handler();
            timeout = false;
        }
    }

    close( listenfd );
    close( pipefd[1] );
    close( pipefd[0] );
    delete [] users;
    return 0;
}
