#include <iostream>
#include <pthread.h>
#include <exception>
#include <semaphore.h>
#include <list>


/*
1. pthread, lock, sem相关的函数没看
2. 线程池怎么调用线程的： 每个线程都会执行 worker 函数，该函数会不断从任务队列中取出任务并执行。
*/

// 互斥锁类
class locker {
public:
    locker() {
        if(pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();
        }
    }

    ~locker() {
        pthread_mutex_destroy(&m_mutex);
    }

    bool lock() {
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    pthread_mutex_t *get()
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};

// 信号量类
class sem {
public:
    sem() {
        if( sem_init( &m_sem, 0, 0 ) != 0 ) {
            throw std::exception();
        }
    }
    sem(int num) {
        if( sem_init( &m_sem, 0, num ) != 0 ) {
            throw std::exception();
        }
    }
    ~sem() {
        sem_destroy( &m_sem );
    }
    // 等待信号量
    bool wait() {
        return sem_wait( &m_sem ) == 0;
    }
    // 增加信号量
    bool post() {
        return sem_post( &m_sem ) == 0;
    }
private:
    sem_t m_sem;
};



template<typename T>
class threadpool {

public:
    // 构造函数
    threadpool(int thread_number = 8, int max_requests = 1000);
    // 析构函数
    ~threadpool();

    // 添加任务
    bool append(T* request);

private:

    static void* worker(void* arg);

    void run();

private:

    int m_thread_number;

    pthread_t* m_threads;

    int m_max_requests;

    std::list< T* > m_workqueue;  

    locker m_queuelocker;

    sem m_queuestat;

    bool m_stop;
};

// 模版类的实现
// 构造函数
template< typename T >
threadpool< T >::threadpool(int thread_number, int max_requests) : 
                m_thread_number(thread_number), m_max_requests(max_requests), 
                m_stop(false), m_threads(NULL){
    
    if((thread_number <= 0) || (max_requests <= 0) ) {
        // #include <exception>
        throw std::exception();
    }
    
    // #include <pthread.h>
    m_threads = new pthread_t[m_thread_number]; 

    for (int i = 0; i < thread_number; ++i){
        printf( "create the %dth thread\n", i);

        // 
        if(pthread_create(m_threads + i, NULL, worker, this) != 0){
            delete [] m_threads;    // 释放动态分配的数组堆内存
            throw std::exception(); 
        }

        if( pthread_detach( m_threads[i] ) ) { 
            delete [] m_threads;
            throw std::exception();
        }
    }

}

template<typename T>
threadpool< T >::~threadpool(){
    delete [] m_threads;
    m_stop = true;
}

// 向任务队列中添加任务
template<typename T>
bool threadpool< T >::append(T* request){

    /*
    1. lock
    2. 判断是否满，未满，加入
    3. unlock
    4. 表示任务数量的信号量+1
    */
    m_queuelocker.lock();
    if (m_workqueue.size() > m_max_requests){
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post(); // +1
    return true;
}

// 调用worker使得线程池开始工作
template< typename T >
void* threadpool< T >::worker( void* arg )
{
    // void* arg是万能指针，用来存储指向threadpool对象的指针；并转换成threadpool* 类型
    threadpool* pool = ( threadpool* )arg;
    pool->run();    
    return pool;
}

template< typename T >
void threadpool< T >::run(){

    // 每个线程都会执行 worker 函数，该函数会不断从任务队列中取出任务并执行。
    while (!m_stop){
        m_queuestat.wait(); // -1
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

class http_conn{

public:
    http_conn(){}
    ~http_conn(){}

public:
    void init(int a, int b);
    void process();

private:
    int m_a;
    int m_b;
    int m_sum;
};

void http_conn::init(int a, int b){

    m_a = a;
    m_b = b;

}

void http_conn::process(){
    m_sum = m_a + m_b;
    printf("%d + %d = %d\n", m_a, m_b, m_sum);
}

int main(int argc, char *argv[]){

    http_conn *users = new http_conn[10];

    for (int i = 0; i < 10; ++i){
        users[i].init(i, i);
    }


    threadpool<http_conn> *m_pool = new threadpool<http_conn>;

    for (int i = 0; i < 10; ++i){
        m_pool->append(users +  i);
    }

    return 0;
}