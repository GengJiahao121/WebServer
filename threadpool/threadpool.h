#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"

/*
总结：
threadpool类是一个线程池类，并定义成了一个模版类，模版参数T是具体的http_conn任务类

模版类包括：模版类数据成员和方法成员的声明和模版类方法的实现

线程池类中有一个任务队列的数据成员，用来存储线程池需要处理的任务
*/

// 模版类的声明
// 线程池类，将它定义为模板类是为了代码复用，模板参数T是任务类
template<typename T>
class threadpool {
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    bool append(T* request); 

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void* worker(void* arg);
    void run();

private:
    // 线程的数量
    int m_thread_number;  
    
    // 描述线程池的数组，大小为m_thread_number    
    pthread_t * m_threads;

    // 请求队列中最多允许的、等待处理的请求的数量  
    int m_max_requests; 
    
    // 请求队列
    std::list< T* > m_workqueue;    // 存储的数据类型为T类型的指针

    // 保护请求队列的互斥锁
    locker m_queuelocker;   

    // 是否有任务需要处理
    sem m_queuestat;    // 信号量

    // 是否结束线程          
    bool m_stop;                     
};

// 模版类的实现
// 构造函数
template< typename T >
threadpool< T >::threadpool(int thread_number, int max_requests) : 
        m_thread_number(thread_number), m_max_requests(max_requests), 
        m_stop(false), m_threads(NULL) {

    if((thread_number <= 0) || (max_requests <= 0) ) {
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_number];     // 在堆内存中分配动态数组，返回值是一个指向分配内存的指针
    if(!m_threads) {
        throw std::exception();
    }

    // 创建thread_number 个线程，并将他们设置为脱离线程。
    for ( int i = 0; i < thread_number; ++i ) {
        printf( "create the %dth thread\n", i);
        // this 表示指向当前对象的指针，当前对象是线程池threadpool实例
        if(pthread_create(m_threads + i, NULL, worker, this ) != 0) {
            delete [] m_threads;    // 释放动态分配的数组堆内存
            throw std::exception(); 
        }
        
        if( pthread_detach( m_threads[i] ) ) {
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template< typename T >
threadpool< T >::~threadpool() {
    delete [] m_threads;    // 动态分配的对象：当使用 new 运算符动态分配的对象通过 delete 或 delete[] 进行释放时，析构函数会被调用。
    m_stop = true;  // ？
}


// 向任务队列中添加任务
template< typename T >
bool threadpool< T >::append( T* request )
{
    // 操作工作队列时一定要加锁，因为它被所有线程共享。
    m_queuelocker.lock();
    if ( m_workqueue.size() > m_max_requests ) {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}


// 线程池中的每个子线程需要去任务队列中取任务，并处理
template< typename T >
void* threadpool< T >::worker( void* arg )
{
    // void* arg是万能指针，用来存储指向threadpool对象的指针；并转换成threadpool* 类型
    threadpool* pool = ( threadpool* )arg;
    pool->run();    
    return pool;
}

// 从任务队列中取任务，需要用到信号量和互斥锁
template< typename T >
void threadpool< T >::run() {

    // 只要线程池实例不结束，就一直等待并接收新任务
    while (!m_stop) {
        m_queuestat.wait();
        m_queuelocker.lock();
        if ( m_workqueue.empty() ) {
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if ( !request ) {
            continue;
        }
        request->process();     // 接收到新任务了就去做任务的处理；request代表一个http_conn任务实例
    }

}

#endif
