/**
 * Created by 刘嘉辉 on 10/22/18.
 * Copyright (c) 2018 刘嘉辉 All rights reserved.
 * @brief To immplmente process_pool.h.
 */

#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

/*封装信号量的类*/
class sem
{
public:
    /*创建并初始化信号量*/
    sem()
    {
        if( sem_init( &m_sem, 0, 0 ) != 0 )
        {
            /*构造函数没有返回值　可以通过抛出异常来报告错误*/
            throw std::exception();
        }
    }

    ~sem()
    {
        sem_destroy( &m_sem );
    }

    /*等待信号量*/
    bool wait()
    {
        return sem_wait( &m_sem ) == 0;
    }
    /*增加信号量*/
    bool post()
    {
        return sem_post( &m_sem ) == 0;
    }

private:
    sem_t m_sem;
};


/*封装互斥锁的类*/
class locker
{
public:
    locker()
    {
        if( pthread_mutex_init( &m_mutex, NULL ) != 0 )
        {
            throw std::exception();
        }
    }
    ~locker()
    {
        pthread_mutex_destroy( &m_mutex );
    }
    bool lock()
    {
        return pthread_mutex_lock( &m_mutex ) == 0;
    }
    bool unlock()
    {
        return pthread_mutex_unlock( &m_mutex ) == 0;
    }

private:
    pthread_mutex_t m_mutex;
};


/*封装条件变量的类*/
class cond
{
public:

    /*创建并初始化*/
    cond()
    {
        if( pthread_mutex_init( &m_mutex, NULL ) != 0 )
        {
            throw std::exception();
        }
        if ( pthread_cond_init( &m_cond, NULL ) != 0 )
        {
            pthread_mutex_destroy( &m_mutex );
            throw std::exception();
        }
    }
    ~cond()
    {
        pthread_mutex_destroy( &m_mutex );
        pthread_cond_destroy( &m_cond );
    }

    /*等待条件变量*/
    bool wait()
    {
        int ret = 0;
        pthread_mutex_lock( &m_mutex );
        ret = pthread_cond_wait( &m_cond, &m_mutex );
        pthread_mutex_unlock( &m_mutex );
        return ret == 0;
    }

    /*唤醒一个等待条件变量的线程*/
    bool signal()
    {
        return pthread_cond_signal( &m_cond ) == 0;
    }

/*锁加条件变量*/
private:
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};

#endif