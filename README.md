# WebServer

## 目录

[1. 服务器的部署](#1)

[2. 技术介绍](#2)

[2.1 线程池 + 非阻塞socket + epoll + 模拟Proactor事务处理模型](#2-1)

[2.2 使 用正则表达式 和有限状态机 解 析GET 和POST 请 求](#2-2)

[2.3 访问服务器数据库实现web端用户注册、登录功能，可以请求服务器图片和视频文件](#2-3)

[2.4 使用基于升序链表的定时器处理非活动连接](#2-4)

[2.5 实现同步/异步日志系统，记录服务器运行状态](#2-5)

[2.6 经Webbench压力测试可以实现上万的并发连接数据交换](#2-6)

[](#)

## <a id="1">1 服务器部署</a>

1、测试前确认已安装MySQL数据库 并 创建对应的数据库、表

安装

```
Ubuntu系统：
// 安装 MySQL 服务器：
sudo apt-get update
sudo apt-get install mysql-server

// 启动 MySQL 服务器：
sudo systemctl start mysql
若要确保 MySQL 在系统启动时自动启动，可以运行以下命令：
sudo systemctl enable mysql

// 安装 MySQL C/C++ 客户端库：
sudo apt-get install libmysqlclient-dev
```

创建

```
create database gjh_webserver;

use gjh_webserver;

create user(name varchar(50), passwd varchar(50));

insert into user (username, passwd) values ("gjh", "gjh");

select * from user;

exit;
```

2、编译

**手动编译时，要添加“-lmysqlclient”选项链接mysql数据库**

**手动编译时，要添加“-pthread”选项链接线程库**

`g++ *.cpp -pthread -lmysqlclient`

3、启动服务器

指定服务器程序端口号：10000

`./a.out 10000`

4、访问服务器

浏览器输入（实现不同功能）：

    登陆页面：
    http://xxx.xxx.xxx.xxx:10000/1

## <a id="2">2 技术介绍</a>

### <a id="2-1">2.1 线程池 + 非阻塞socket + epoll + 模拟Proactor事务处理模型</a>

**1、 线程池**

服务器程序相当于一个进程，在程序启动时就创建拥有指定数量的线程构成线程池

进程都做哪些工作：

1. 服务器的运行 
2. 监听事件、分析事件
    1. 管理用户（用户的初始化、读取用户数据的到达、向用户写缓冲区中写数据）
    2. 管理定义器、定义器管道的读写
3. 日志系统管理
3. 数据库相关

线程都做哪些工作：

1. 解析用户到达的数据，分析请求，注册写事件

**2、非阻塞socket**

当检测某个文件描述符是否有时间发生时：阻塞模式为一只阻塞，直到有事件发生；非阻塞模式为直接返回是否发生事件。

**3、epoll**

一种I/O多路复用技术，可以同时监听多个I/O事件（文件描述符）的发生。

文件描述符会发生事件：

1. 新用户建立连接 
2. 用户内部错误 
3. 定时器相关 
4. 客户端发送数据 
5. 有写事件发生

针对每种事件，都会有对应的具体的操作

具体的原理：

**4、模拟Proactor事务处理模型**

![Alt text](image.png)

### <a id='2-2'>2.2 使用正则表达式和有限状态机解析GET和POST请求</a>

### <a id='2-3'>2.3 访问服务器数据库实现web端用户注册、登录功能，可以请求服务器图片和视频文件</a>

### <a id='2-4'>2.4 使用基于升序链表的定时器处理非活动连接</a>

### <a id='2-5'>2.5 实现同步/异步日志系统，记录服务器运行状态</a>

### <a id='2-6'>2.6 经Webbench压力测试可以实现上万的并发连接数据交换</a>

### <a id=''></a>

简历上的所有功能已经实现

1. 还需要完善github
2. 梳理每个功能的流程，行程笔记和流程图
3. 重构代码
4. 使用cmake自动构建代码



