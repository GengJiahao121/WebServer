#include "webserver.h"

WebServer::WebServer(){

    // http_conn类对象
    users = new http_conn[MAX_FD];

    //root文件夹路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[11] = "/resources";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    //定时器
    users_timer = new client_data[MAX_FD];
}


WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}


void WebServer::init(int port , string user, string passWord, string databaseName, 
                int log_write, int sql_num, int thread_num, int close_log, int epoll_et)
{
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_log_write = log_write;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_close_log = close_log;
    m_epoll_et = epoll_et;
}


void WebServer::log_write(){
    if (0 == m_close_log)
    {
        //初始化日志
        if (1 == m_log_write){
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
            LOG_INFO("异步日志系统开启。");
        } 
        else{
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
        }        
    }
}

void WebServer::sql_pool(){
    //初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    //初始化数据库读取表
    users->initmysql_result(m_connPool);
}

void WebServer::thread_pool(){

        m_pool = new threadpool<http_conn>;

}

void WebServer::eventListen(){

    m_listenfd = socket( PF_INET, SOCK_STREAM, 0 );  
    assert(m_listenfd >= 0);

    // 将本机的一些信息添加到sockaddr_in中
    int ret = 0;
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons( m_port );

    // 端口复用 目的是用一个端口处理所有的用户连接请求
    int reuse = 1;
    setsockopt( m_listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );

    ret = bind( m_listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    assert(ret >= 0);
    ret = listen( m_listenfd, 5 );
    assert(ret >= 0);

    utils.init(TIMESLOT);

    // 创建epoll对象，和事件数组，添加
    epoll_event events[ MAX_EVENT_NUMBER ];
    m_epollfd = epoll_create( 5 );
    utils.addfd( m_epollfd, m_listenfd, false);
    http_conn::m_epollfd = m_epollfd;


    // 定时器相关
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false);

    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler);
    utils.addsig(SIGTERM, utils.sig_handler);

    alarm(TIMESLOT);

    //工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;

}

void WebServer::timer(int connfd, struct sockaddr_in client_address)
{   
    printf("m_root = %s\n", m_root);
    users[connfd].init(connfd, client_address, m_root, m_close_log, m_user, m_passWord, m_databaseName, m_epoll_et);

    char *doc_root = users[connfd].get_Doc_root();
    char *m_user = users[connfd].get_m_user();
    printf("users[connfd]->doc_root = %s\n", doc_root);
    printf("users[connfd]->m_user = %s\n)", m_user);

    //初始化client_data数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

bool WebServer::dealclientdata(){

    // 1. 获取要连接客户的ip地址与端口号
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof( client_address );

    // 2. 建立连接
    int connfd = accept( m_listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
    if ( connfd < 0 ) {
        LOG_ERROR("error is: %d", errno);
        printf( "errno is: %d\n", errno );
        return false;
    } 

    // 3. 最大连接用户数限制
    if( http_conn::m_user_count >= MAX_FD ) {
        utils.show_error(connfd, "Internal server busy");
        LOG_ERROR("%s", "Internal server busy");
        return false;
    }

    // 4. 定时器 与 用户的初始化
    timer(connfd, client_address);
    
    return true;
}

void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
    {
        utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                timeout = true;
                break;
            }
            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

void WebServer::dealwithread(int sockfd)
{
    util_timer* timer = users_timer[sockfd].timer;

    // 从fd对应的缓冲区中读取数据
    // 通过read()函数读取文件描述符中到达的数据，如何读取成功，将封装好的http_conn对象加入到线程池队列，等待被处理（解析）
    // 主线程读数据，读完了让线程池去解析数据
    if(users[sockfd].read()) 
    { 
        LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
        printf("******************************");

        m_pool->append(users + sockfd);
        if (timer){
            adjust_timer(timer);
        }
    }
    else 
    {
        deal_timer(timer, sockfd);
    }
}

void WebServer::dealwithwrite(int sockfd){

    util_timer *timer = users_timer[sockfd].timer;

    //proactor
    if (users[sockfd].write())
    {
        LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

        if (timer)
        {
            adjust_timer(timer);
        }
    }
    else
    {
        deal_timer(timer, sockfd);
    }
}


void WebServer::eventLoop()
{

    bool timeout = false;
    bool stop_server = false;

    while (!stop_server)
    {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;

            //处理新到的客户连接
            if (sockfd == m_listenfd)
            {
                bool flag = dealclientdata();
                if (false == flag)
                    continue;
            }
            // 处理信号
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            //处理信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            //处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                dealwithread(sockfd);
            }
            else if (events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }
        if (timeout)
        {
            utils.timer_handler();

            LOG_INFO("%s", "timer tick");

            timeout = false;
        }

    }
}


















