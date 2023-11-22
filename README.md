# WebServer

## 目录

[1. 服务器的部署](#1)

[2. 技术介绍](#2)

[2.1 线程池 + 非阻塞socket + epoll + 模拟Proactor事务处理模型](#2-1)


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













到2023年11月7日止：

简历上的所有功能已经实现

1. 还需要完善github
2. 梳理每个功能的流程，行程笔记和流程图
3. 重构代码
4. 使用cmake自动构建代码

到2023年10月13日止：

已实现：

1. 使用线程池 + 非阻塞socket + epoll + 模拟Proactor事务处理模型
2. 使用正则表达式 和有限状态机 解 析GET请求
3. 使用基于升序链表的定时器处理非活动连接
4. 实现同步/异步日志系统，记录服务器运行状态
5. 经Webbench压力测试可以实现上万的并发连接数据交换

未实现：

访问服务器数据库实现web端用户注册、登录功能，可以请求服务器图片和视频文件
