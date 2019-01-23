#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <iostream>

/*子进程类*/
class process
{
public:
    /*以 -1初始化*/
    process() : m_pid( -1 ){}

public:
    /*子进程号*/
    pid_t m_pid;
    /*父子进程通信管道*/
    int m_pipefd[2];
};

/*进程池类
 *其模板参数是处理逻辑任务的类
 **/
template< typename T >
class processpool
{
    /*将构造函数定义为私有的　因此我们只能通过后边的create静态*/
private:
    processpool( int listenfd, int process_number = 8 );

public:
    /*单例模式 在之后调用到*/
    static processpool< T >* create( int listenfd, int process_number = 8 )
    {
        if( !m_instance )
        {
            m_instance = new processpool< T >( listenfd, process_number );
        }
        return m_instance;
    }
    
    ~processpool()
    {
        delete [] m_sub_process;
    }
    
    /*启动进程池*/
    void run();

private:
    void setup_sig_pipe();
    void run_parent();
    void run_child();

private:
    /*进程允许的最大子进程数*/
    static const int MAX_PROCESS_NUMBER = 16;
    /*每个子进程最多能处理的客户数量*/
    static const int USER_PER_PROCESS = 65536;
    /*epoll 最多能处理的事件数*/
    static const int MAX_EVENT_NUMBER = 10000;
    /*进程池中的进程数*/
    int m_process_number;
    /*进程池在池中的序号　从０开始*/
    int m_idx;
    /*每个进程都有一个epoll内核事件表 用epollfd标识*/
    int m_epollfd;
    /*监听socket*/
    int m_listenfd;
    /*子进程通过stop来决定是否停止*/
    int m_stop;
    /*保存所有的子进程的描述信息*/
    process* m_sub_process;
    /*进程池静态实例*/
    static processpool< T >* m_instance;
};



template< typename T >
processpool< T >* processpool< T >::m_instance = NULL;

/*用于处理信号的管道　实现统一信号源
* 全局 */
static int sig_pipefd[2];

static int setnonblocking( int fd )
{
    int old_option = fcntl( fd, F_GETFL );
    int new_option = old_option | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_option );
    return old_option;
}

static void addfd( int epollfd, int fd )
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    setnonblocking( fd );
}

static void removefd( int epollfd, int fd )
{
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
    close( fd );
}

static void sig_handler( int sig )
{
    int save_errno = errno;
    int msg = sig;
    send( sig_pipefd[1], ( char* )&msg, 1, 0 );
    errno = save_errno;
}

static void addsig( int sig, void( handler )(int), bool restart = true )
{
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler;
    if( restart )
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}


/*进程池的构造函数 参数listenfd是监听*/
template< typename T >
processpool< T >::processpool( int listenfd, int process_number ) 
    : m_listenfd( listenfd ), m_process_number( process_number ), m_idx( -1 ), m_stop( false )
{
    assert( ( process_number > 0 ) && ( process_number <= MAX_PROCESS_NUMBER ) );
    m_sub_process = new process[ process_number ];
    assert( m_sub_process );

    for( int i = 0; i < process_number; ++i )
    {
        /*建立通信管道*/
        int ret = socketpair( PF_UNIX, SOCK_STREAM, 0, m_sub_process[i].m_pipefd );
        assert( ret == 0 );

        m_sub_process[i].m_pid = fork();
        assert( m_sub_process[i].m_pid >= 0 );
        /*父进程*/
        if( m_sub_process[i].m_pid > 0 )
        {
            close( m_sub_process[i].m_pipefd[1] );
            continue;
        }
        /*子进程*/
        else
        {
            close( m_sub_process[i].m_pipefd[0] );
            m_idx = i;
            break;
        }
    }
}


/*统一事件源*
 * 每一个子进程都会拥有一次 sig_pipefd
　父子进程都做一次*/
template< typename T >
void processpool< T >::setup_sig_pipe()
{
    m_epollfd = epoll_create( 5 );
    assert( m_epollfd != -1 );

    int ret = socketpair( PF_UNIX, SOCK_STREAM, 0, sig_pipefd );
    assert( ret != -1 );

    setnonblocking( sig_pipefd[1] );
    addfd( m_epollfd, sig_pipefd[0] );

    /*设置信号处理函数*/
    addsig( SIGCHLD, sig_handler );
    addsig( SIGTERM, sig_handler );
    addsig( SIGINT, sig_handler );
    addsig( SIGPIPE, SIG_IGN );
}

/*父进程中的m_idx是-1 子进程中的m_idx值大于等于0　我们据此判断接下来要运行的代码是父进程代码还是子进程的 */
template< typename T >
void processpool< T >::run()
{
    if( m_idx != -1 )
    {
        run_child();
        return;
    }
    run_parent();
}


/*子进程运行*/
template< typename T >
void processpool< T >::run_child()
{
    /*用于父子进程通信*/
    setup_sig_pipe();

    /*每个子进程都能通过其在进程池中的序号值m_idx找到与父进程通信的管道*/
    int pipefd = m_sub_process[m_idx].m_pipefd[ 1 ];
    addfd( m_epollfd, pipefd );

    epoll_event events[ MAX_EVENT_NUMBER ];
    
    /*处理cgi请求的类 一个子进程最多处理USER_PER_PROCESS个连接*/
    T* users = new T [ USER_PER_PROCESS ];
    assert( users );
    int number = 0;
    int ret = -1;

    while( ! m_stop )
    {
        number = epoll_wait( m_epollfd, events, MAX_EVENT_NUMBER, -1 );
        if ( ( number < 0 ) && ( errno != EINTR ) )
        {
            printf( "epoll failure\n" );
            break;
        }

        for ( int i = 0; i < number; i++ )
        {
            int sockfd = events[i].data.fd;
            /*从父子进程之间的管道读取数据 并将结果保存在变量client中 如果成功表示有新客户连接到来*/
            if( ( sockfd == pipefd ) && ( events[i].events & EPOLLIN ) )
            {
                int client = 0;
                /*仅接收　不处理字符串*/
                ret = recv( sockfd, ( char* )&client, sizeof( client ), 0 );
                if( ( ( ret < 0 ) && ( errno != EAGAIN ) ) || ret == 0 ) 
                {
                    continue;
                }
                /*让子进程来完成客户端的连接*/
                else
                {
                    struct sockaddr_in client_address;
                    socklen_t client_addrlength = sizeof( client_address );
                    int connfd = accept( m_listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                    if ( connfd < 0 )
                    {
                        printf( "errno is: %d\n", errno );
                        continue;
                    }
                    addfd( m_epollfd, connfd );
                    
                    /* 模板类T必须实现init方法　以初始化一个客户端连接 
                     * 我们直接使用connfd来索引逻辑处理对象,以提高程序的运行效率 
                     * 但有一个问题就是有些浪费资源　hash_map获取会更好一些
                     */
                    users[connfd].init( m_epollfd, connfd, client_address );
                }
            }

            /* 处理子进程收到信号
             * 关于这块大家要小心处理
             * 因为父子进程会同时收到完全一样的信号
             */
            else if( ( sockfd == sig_pipefd[0] ) && ( events[i].events & EPOLLIN ) )
            {
                int sig;
                char signals[1024];
                ret = recv( sig_pipefd[0], signals, sizeof( signals ), 0 );
                if( ret <= 0 )
                {
                    continue;
                }
                else
                {
                    for( int i = 0; i < ret; ++i )
                    {
                        switch( signals[i] )
                        {
                            /*子进程不需要处理这个child新号*/
                            case SIGCHLD:
                            case SIGTERM:
                            case SIGINT:
                            {
                                printf("子进程也受到啦\n");
                                m_stop = true;
                                break;
                            }
                            default:
                            {
                                break;
                            }
                        }
                    }
                }
            }

            /*客户请求的到来　调用process来处理*/
            else if( events[i].events & EPOLLIN )
            {
                users[sockfd].process();
            }
            else
            {
                continue;
            }
        }
    }

    delete [] users;
    users = NULL;
    close( pipefd );
    //close( m_listenfd );
    close( m_epollfd );
}

template< typename T >
void processpool< T >::run_parent()
{
    setup_sig_pipe();

    /*父进程监听listenfd*/
    addfd( m_epollfd, m_listenfd );

    epoll_event events[ MAX_EVENT_NUMBER ];
    int sub_process_counter = 0;
    int new_conn = 1;
    int number = 0;
    int ret = -1;

    while( ! m_stop )
    {
        number = epoll_wait( m_epollfd, events, MAX_EVENT_NUMBER, -1 );
        if ( ( number < 0 ) && ( errno != EINTR ) )
        {
            printf( "epoll failure\n" );
            break;
        }

        for ( int i = 0; i < number; i++ )
        {
            int sockfd = events[i].data.fd;
            
            /*有新连接到来采用Round Robin方式将其分配给一个子进程处理*/
            if( sockfd == m_listenfd )
            {
                int i =  sub_process_counter;
                do
                {
                    if( m_sub_process[i].m_pid != -1 )
                    {
                        break;
                    }
                    i = (i+1)%m_process_number;
                }
                while( i != sub_process_counter );
                
                if( m_sub_process[i].m_pid == -1 )
                {
                    m_stop = true;
                    break;
                }
                
                sub_process_counter = (i+1)%m_process_number;
                
                /*主进程发送信息通知紫禁城接受连接*/
                send( m_sub_process[i].m_pipefd[0], ( char* )&new_conn, sizeof( new_conn ), 0 );
                
                printf( "send request to child %d\n", i );
            }

            /*处理父进程接收到的信号*/
            else if( ( sockfd == sig_pipefd[0] ) && ( events[i].events & EPOLLIN ) )
            {
                int sig;
                char signals[1024];
                ret = recv( sig_pipefd[0], signals, sizeof( signals ), 0 );
                if( ret <= 0 )
                {
                    continue;
                }
                else
                {
                    for( int i = 0; i < ret; ++i )
                    {
                        switch( signals[i] )
                        {
                            /*第i个子进程退出了　则主进程关闭相应的通信管道并设置m_pid为-1 已标记子进程已退出*/
                            case SIGCHLD:
                            {
                                pid_t pid;
                                int stat;
                                while ( ( pid = waitpid( -1, &stat, WNOHANG ) ) > 0 )
                                {
                                    for( int i = 0; i < m_process_number; ++i )
                                    {
                                        if( m_sub_process[i].m_pid == pid )
                                        {
                                            printf( "child %d join\n", i );
                                            close( m_sub_process[i].m_pipefd[0] );
                                            m_sub_process[i].m_pid = -1;
                                        }
                                    }
                                }
                                /*检查是否有子进程存活*/
                                m_stop = true;
                                for( int i = 0; i < m_process_number; ++i )
                                {
                                    if( m_sub_process[i].m_pid != -1 )
                                    {
                                        m_stop = false;
                                    }
                                }
                                break;
                            }
                            case SIGTERM:
                            /*父进程终止信号　杀死所有子进程并等待救赎　
                            * 更好的方式是向父子进程的通信管道发送特殊数据*/
                            case SIGINT:
                            {
                                printf( "kill all the clild now\n" );
                                for( int i = 0; i < m_process_number; ++i )
                                {
                                    int pid = m_sub_process[i].m_pid;
                                    if( pid != -1 )
                                    {
                                        kill( pid, SIGTERM );
                                    }
                                }
                                break;
                            }
                            default:
                            {
                                break;
                            }
                        }
                    }
                }
            }
            else
            {
                continue;
            }
        }
    }

    /*由创建者关闭*/
    close( m_epollfd );
}

#endif
