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

#include "processpool.h"


class cgi_conn
{
public:
    cgi_conn(){}
    ~cgi_conn(){}
    //初始化客户端连接，清空该缓冲区
    void init(int epollfd, int sockfd, const sockaddr_in& client_addr)
    {
        m_epollfd = epollfd;
        m_sockfd  = sockfd;
        m_address = client_addr;
        memset(m_buf, '\0', BUFFER_SIZE);
        m_read_idx = 0;
    }
    /*
     * 从m_sockfd读入信息，并进行处理
     */
    void process()
    {
        int idx = 0;
        int ret = -1;
        //循环读取和分析客户的数据
        while(true)
        {
            idx = m_read_idx;
            ret = recv(m_sockfd, m_buf + idx, BUFFER_SIZE-1-idx, 0);

            if(ret < 0)
            {
                if( errno != EAGAIN)
                {
                    removefd(m_epollfd, m_sockfd);
                }
            }
            //如果对方关闭链接，服务器端也关闭
            else if(ret == 0)
            {
                removefd(m_epollfd, m_sockfd);
                break;
            }
            else
            {
                m_read_idx += ret;
                std::cout<<"user content is : "<<m_buf<<std::endl;

                for(;idx < m_read_idx; ++idx)
                {
                    if( ( idx >= 1) && ( m_buf[idx-1] == '\r' && m_buf[idx] == '\n' ) )
                    {
                        break;
                    }
                }
                //如果没有遇到字符 \r\n 就读取更多数据
                if(idx == m_read_idx)
                {
                    continue;
                }
                m_buf[ idx-1 ] = '\0';

                char * file_name = m_buf;
                /*判断客户需要运行的cgi程序是否存在*/
                if(access(file_name, F_OK) == -1)
                {
                    removefd(m_epollfd, m_sockfd);
                    break;
                }

                /*创建子进程来执行cgi程序*/
                ret = fork();
                if(ret == -1)
                {
                    /*父进程关闭连接就可以了*/
                    removefd(m_epollfd,m_sockfd);
                    break;
                }
                //父进程
                else if(ret > 0)
                {
                    /*因为父进程并不处理这件事，子进程会共享m_epollfd*/
                    removefd(m_epollfd,m_sockfd);
                    break;
                }
                else
                {
                    /*子进程将标准输出定向到m_sockfd,并执行CGI程序*/
                    close(1);
                    close(2);
                    /*
                     * 不继承原有的文件描述符属性　close-on-exec non-blocking
                     * 返回一个最小的文件描述符与原来具有相同所指
                     * 已关闭的STDERR_FILENO是１ 所以　execl 本来要发向1 然后就发到了m_socked那里去
                     */
                    dup(m_sockfd);
                    /*
                     * execl()用来执行参数path 字符串所代表的文件路径, 
                     * 接下来的参数代表执行该文件时传递过去的argv(0),argv[1], ..., 
                     * 最后一个参数必须用空指针(NULL)作结束.
                     * 示例 execl("/bin/ls", "ls", NULL);
                     */
                    
                   // execl(m_buf, "sl", NULL);
                    execl(m_buf, "ls", NULL);
                    exit(0);
                }
            }
        }
    }

private:
    //读缓冲区大小
    static const int BUFFER_SIZE = 1024;
    static int m_epollfd;
    int m_sockfd;
    sockaddr_in m_address;
    char m_buf[ BUFFER_SIZE ];

    //标记缓冲区下一个要读入的位置
    int m_read_idx;
};

int cgi_conn::m_epollfd = -1;


int main(int argc, char* argv[])
{
    if(argc <= 2)
    {
        printf("usage: %s ip_address port_number\n",basename(argv[0]));
        return 1;
    }
    const char * ip = argv[1];
    int port = atoi(argv[2]);


    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert( listenfd >=0 );
    int ret = 0;

    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    ret= bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret != -1);
    ret = listen(listenfd,5);
    assert(ret != -1);


    processpool<cgi_conn>* pool = processpool<cgi_conn>::create(listenfd);
    if(pool)
    {
        pool->run();
        delete pool;
    }
    close(listenfd);

    return 0;
}



