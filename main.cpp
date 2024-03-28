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
#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"
#include "lst_timer.h"
#include "log.h"
#include "config.h"
#include "sql_connection_pool.h"

#define MAX_FD 65536   // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000  // 监听的最大的事件数量

#define TIMESLOT 5

static int epollfd;

static int pipefd[2];  // 读端和写端 
static sort_timer_lst timer_lst;    // 定时器双端链表

// 添加文件描述符
extern void addfd( int epollfd, int fd, bool one_shot );
extern void removefd( int epollfd, int fd );

// 定时器相关函数
extern int setnonblocking( int fd );
extern void addfd( int epollfd, int fd );
// extern void cb_func( client_data* user_data, int epollfd);
// extern void timer_handler(sort_timer_lst& timer_lst, int epollfd);

// **************定时器信号****************
// 定时器信号处理函数
void sig_handler( int sig )
{
    int save_errno = errno;
    int msg = sig;
    send( pipefd[1], ( char* )&msg, 1, 0 );     // 发送
    errno = save_errno;
}

// 当指定的信号发生时，执行sig_handler回调函数
void addsig( int sig)
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
// *************定时器信号****************


// 捕捉到sig信号后，做handler处理
void addsig(int sig, void( handler )(int)){
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler;
    sigfillset( &sa.sa_mask ); // 将信号集中的所有的标志位置为1； sa.sa_mask  临时阻塞信号集，在信号捕捉函数执行过程中，临时阻塞某些信号。
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

int main( int argc, char* argv[] ) {
    
    if( argc <= 1 ) {
        printf( "usage: %s port_number\n", basename(argv[0]));
        return 1;
    }

    // 将字符串转换为整数
    int port = atoi( argv[1] ); 

    Config config;

    // 日志系统 
    // 异步方式
    // int m_close_log = 0;
    int m_close_log = config.m_close_log;
    Log::get_instance()->init("./ServerLog", config.m_close_log, 2000, 800000, 800);
    // 同步方式
    //Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);

    // 数据库连接池
    connection_pool *m_connPool;
    string m_user = "root";         //登陆数据库用户名
    string m_passWord = "root";     //登陆数据库密码
    string m_databaseName = "gjh_webserver"; //使用数据库名
    int m_sql_num = 5;
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);
    //初始化数据库读取表
    http_conn *user;
    user->initmysql_result(m_connPool);

    // 让当前进程忽略sigpipe信号
    addsig( SIGPIPE, SIG_IGN ); 

    // 实例化线程池
    threadpool< http_conn >* pool = NULL;
    try {
        pool = new threadpool<http_conn>; 
    } catch( ... ) {
        return 1;
    }

    http_conn* users = new http_conn[ MAX_FD ];

    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );   // 创建socket并返回对应的文件描述符
    // 将本机的一些信息添加到sockaddr_in中
    int ret = 0;
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons( port );
    // 端口复用
    int reuse = 1;
    setsockopt( listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );

    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    ret = listen( listenfd, 5 );

    // 创建epoll对象，和事件数组，添加
    epoll_event events[ MAX_EVENT_NUMBER ];
    epollfd = epoll_create( 5 );
    addfd( epollfd, listenfd, false );
    http_conn::m_epollfd = epollfd;

    // 定时器
    // 创建管道
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert( ret != -1 );
    setnonblocking( pipefd[1] );    // 设置为非阻塞
    addfd( epollfd, pipefd[0] );    // 添加到epoll监听数组中，监听的事件为epollin，说明pipefd[0]为读端

    client_data* users_timer = new client_data[MAX_FD];  
    bool timeout = false;
    alarm(TIMESLOT);  // 定时,5秒后产生SIGALARM信号

    // 设置信号处理函数
    addsig( SIGALRM );  // 当发生SIGALRM信号时（超时），向管道中写数据
    addsig( SIGTERM );  // 程序正常结束时，向管道中写数据
    bool stop_server = false;

    // 服务器运行
    while(true) {
        
        if (stop_server == true){
            break;
        }

        // events 传出参数，保存了发送了变化的文件描述符的信息
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        
        if ( ( number < 0 ) && ( errno != EINTR ) ) {
            LOG_ERROR("%s", "epoll failure");
            printf( "epoll failure\n" );
            break;
        }

        for ( int i = 0; i < number; i++ ) {
            
            int sockfd = events[i].data.fd;
            
            // 有新的连接进来
            if( sockfd == listenfd ) {
                
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                if ( connfd < 0 ) {
                    LOG_ERROR("error is: %d", errno);
                    printf( "errno is: %d\n", errno );
                    continue;
                } 

                if( http_conn::m_user_count >= MAX_FD ) {
                    close(connfd);
                    continue;
                }
                users[connfd].init( connfd, client_address);

                // 定时器
                users_timer[connfd].address = client_address;
                users_timer[connfd].sockfd = connfd;

                // 创建定时器，设置其回调函数与超时时间，然后绑定定时器与用户数据，最后将定时器添加到链表timer_lst中
                util_timer* timer = new util_timer;
                timer->user_data = &users_timer[connfd];
                timer->cb_func = cb_func;
                time_t cur = time( NULL );
                timer->expire = cur + 3 * TIMESLOT;
                users_timer[connfd].timer = timer;
                timer_lst.add_timer( timer );

            // 该连接内部发生错误，关闭连接
            } else if( events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR ) ) {
                
                users[sockfd].close_conn();

                util_timer* timer = users_timer[sockfd].timer;
                timer_lst.del_timer(timer);

            // 定时器超时并向管道的写入数据，epoll监测到该文件描述符有读事件发生
            } else if( ( sockfd == pipefd[0] ) && ( events[i].events & EPOLLIN ) ) {    // 管道有可读事件进来

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
                                // users[sockfd].close_conn();
                            }
                        }
                    }
                }

            // 如果sockfd发生除了管道读事件之外的读事件，那么就是有客户端发送过来数据，需要服务器的主线程将数据读到读缓冲区
            } else if(events[i].events & EPOLLIN) {

                util_timer* timer = users_timer[sockfd].timer;
                
                if(users[sockfd].read()) {
                    // 通过read()函数读取文件描述符中到达的数据，如何读取成功，将封装好的http_conn对象加入到线程池队列，等待被处理（解析）
                    pool->append(users + sockfd);

                    // 如果某个客户端上有数据可读，则我们要调整该连接对应的定时器，以延迟该连接被关闭的时间。
                    if( timer ) {
                        time_t cur = time( NULL );
                        timer->expire = cur + 3 * TIMESLOT;
                        LOG_INFO("adjust timer once");
                        printf( "adjust timer once\n" );
                        timer_lst.adjust_timer( timer );
                    }

                } else {
                    users[sockfd].close_conn();
                    timer_lst.del_timer(timer);
                }



            }  else if( events[i].events & EPOLLOUT ) {

                util_timer* timer = users_timer[sockfd].timer;
                
                if( !users[sockfd].write() ) {
                    users[sockfd].close_conn();
                    timer_lst.del_timer(timer);
                }

            } 
        }

        // 最后处理定时事件，因为I/O事件有更高的优先级。当然，这样做将导致定时任务不能精准的按照预定的时间执行。
        if( timeout ) {
            timer_handler();
            timeout = false;
        }
    }
    
    close( epollfd );
    close( listenfd );
    close(pipefd[1]);
    close(pipefd[0]);
    delete [] users_timer;
    delete [] users;
    delete pool;
    return 0;
}