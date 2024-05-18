
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>

int main(int argc, int *argv[]){
    // 1. 创建socket
    /*
    #include <unistd.h> -> PF_INET, SOCK_STREAM
    */
    int lfd = socket(PF_INET, SOCK_STREAM, 0);

    // 2. 绑定本机的地址
    struct sockaddr_in saddr;
    saddr.sin_port = htons(9999); // 字节序转换函数。h主机字节序 -> to转换 -> ns网络字节序。
    saddr.sin_family = AF_INET; // IPv4
    saddr.sin_addr.s_addr = INADDR_ANY; // 使用 INADDR_ANY 可以使服务器监听所有可用的网络接口上的连接请求，而不需要关心服务器运行在哪个具体的IP地址上。

    bind(lfd, (struct sockaddr *)&saddr, sizeof(saddr));

    // 3. 监听socket连接
    listen(lfd, 8);

    // 4. 创建epoll，绑定监听事件，插入监听fd
    int epfd = epoll_create(100);
    struct epoll_event epev;
    epev.events = EPOLLIN;
    epev.data.fd = lfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &epev);

    struct epoll_event epevs[1024];

    while(1) {
        int ret = epoll_wait(epfd, epevs, 1024, -1); // 1024: 第二个参数结构体数组的大小 -1: timeout : 阻塞时间 0 : 不阻塞 -1 : 阻塞，直到检测到fd数据发生变化，解除阻塞 > 0 : 阻塞的时长(毫秒)
        if(ret == -1) {
            perror("epoll_wait");
            exit(-1);
        }

        printf("ret = %d\n", ret);

        for(int i = 0; i < ret; i++) {

            int curfd = epevs[i].data.fd;
            if(curfd == lfd) {
                // 监听的文件描述符有数据达到，有客户端连接
                struct sockaddr_in cliaddr;
                int len = sizeof(cliaddr);
                int cfd = accept(lfd, (struct sockaddr *)&cliaddr, &len);

                // 设置cfd属性非阻塞
                // #include <fcntl.h>
                int flag = fcntl(cfd, F_GETFL);
                flag | O_NONBLOCK;
                fcntl(cfd, F_SETFL, flag);

                epev.events = EPOLLIN | EPOLLET;    // 设置边沿触发
                epev.data.fd = cfd;
                epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &epev);
            }else{
                if(epevs[i].events & EPOLLOUT) {
                    continue;
                }  

                // 循环读取出所有数据
                char buf[5];
                int len = 0;
                while( (len = read(curfd, buf, sizeof(buf))) > 0) {
                    // 打印数据
                    // printf("recv data : %s\n", buf);
                    write(STDOUT_FILENO, buf, len);
                    write(curfd, buf, len);
                }
                if(len == 0) {
                    printf("client closed....");
                }else if(len == -1) {
                    if(errno == EAGAIN) {
                        printf("data over.....");
                    }else {
                        perror("read");
                        exit(-1);
                    }
                    
                }
            }
    }


    }

}