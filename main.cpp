#include "./config/config.h"

int main(int argc, char *argv[]){

    if( argc <= 1 ) {
        printf( "usage: %s port_number\n", basename(argv[0]));
        return 1;
    }

    //需要修改的数据库信息,登录名,密码,库名
    string user = "root";
    string passwd = "root";
    string databasename = "gjh_webserver";

    // 加载参数
    Config config;
    config.parse_arg(argc, argv);

    WebServer server;
    server.init(config.PORT, user, passwd, databasename, config.LOGWrite, 
                config.sql_num, config.thread_num, config.close_log, config.epoll_et);

    // 日志系统
    server.log_write();

    // 数据库
    server.sql_pool();

    //线程池
    server.thread_pool();

    //监听
    server.eventListen();

    //运行
    server.eventLoop();

    return 0;
}