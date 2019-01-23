#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "locker.h"


/*线程池类，将它定义为模板类是为了代码复用。模板参数T是任务类*/
/*今天听到有人说模板类只能放在头文件里*/
template< typename T >
class threadpool
{
public:
    /*thread_number 是线程数量　最多允许10000个*/
    threadpool( int thread_number = 8, int max_requests = 10000 );
    ~threadpool();
    /*往请求队列中添加任务*/
    bool append( T* request );

private:
    /*工作线程运行的函数 它不断地从工农工作队列中取出任务并执行之*/
    static void* worker( void* arg );
    void run();

private:
    /*池中线程数*/
    int m_thread_number;
    /*请求队列中允许的最大请求数*/
    int m_max_requests;
    /*描述线程池的数组其大小为m_thread_number*/
    pthread_t* m_threads;
    /*请求队列*/
    std::list< T* > m_workqueue;
    /*保护请求队列的互斥锁*/
    locker m_queuelocker;
    /*是否有任务需要处理*/
    sem m_queuestat;
    /*是否结束线程*/
    bool m_stop;
};

template< typename T >
threadpool< T >::threadpool( int thread_number, int max_requests ) : 
        m_thread_number( thread_number ), m_max_requests( max_requests ), m_stop( false ), m_threads( NULL )
{
    if( ( thread_number <= 0 ) || ( max_requests <= 0 ) )
    {
        throw std::exception();
    }

    m_threads = new pthread_t[ m_thread_number ];
    if( ! m_threads )
    {
        throw std::exception();
    }
    /*创建thread_num个线程　并将他们都设置为脱离线程 ?*/ 
    for ( int i = 0; i < thread_number; ++i )
    {
        printf( "create the %dth thread\n", i );
        /*关于数据的传法这里比较有意思了 this　这个是将线程池的这个类整体传了进去*/
        if( pthread_create( m_threads + i, NULL, worker, this ) != 0 )
        {
            delete [] m_threads;
            throw std::exception();
        }
        //脱离线程是什么意思呢
        if( pthread_detach( m_threads[i] ) )
        {
            delete [] m_threads;
            throw std::exception();
        }
    }
}

/*析构函数　释放那个资源*/
template< typename T >
threadpool< T >::~threadpool()
{
    delete [] m_threads;
    m_stop = true;
}

template< typename T >
bool threadpool< T >::append( T* request )
{
    /*上锁*/
    m_queuelocker.lock();

    /*任务请求队列大于最大请求数　舍弃掉不需要再添加了*/
    if ( m_workqueue.size() > m_max_requests )
    {
        m_queuelocker.unlock();
        return false;
    }
    /*添加进队列之中*/
    m_workqueue.push_back( request );
    m_queuelocker.unlock();
    
    /**/
    m_queuestat.post();
    return true;
}


/*线程运行的函数*/
template< typename T >
void* threadpool< T >::worker( void* arg )
{
    threadpool* pool = ( threadpool* )arg;
    pool->run();
    return pool;
}

/*在对列中取任务进行执行*/
template< typename T >
void threadpool< T >::run()
{
    while ( ! m_stop )
    {
        /*是否有任务需要处理的信号量*/
        m_queuestat.wait();
        /*枷锁*/
        m_queuelocker.lock();
        if ( m_workqueue.empty() )
        {
            /*空就不用取了嘛*/
            m_queuelocker.unlock();
            continue;
        }

        /*取出第一个任务来处理*/
        T* request = m_workqueue.front();
        /*删除第一个*/
        m_workqueue.pop_front();
        /*解锁*/
        m_queuelocker.unlock();
        if ( ! request )
        {
            /*可能为空*/
            continue;
        }
        /*执行之*/
        request->process();
    }
}

#endif
