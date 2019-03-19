/**
 * Created by 刘嘉辉 on 10/21/18.
 * Copyright (c) 2018 刘嘉辉 All rights reserved.
 * @brief To immplmente process_pool.h.
 */

#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/uio.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include "locker.h"

class http_conn
{
public:
    static const int FILENAME_LEN = 200;
    /*读缓冲区的大小*/
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    /*HTTP请求方法*/
    enum METHOD { GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH };
    /*解析客户时主状态机所处的状态*/
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
    /*请求结果*/
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    /*行读取结果*/
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

public:
    http_conn(){}
    ~http_conn(){}

public:
    /*初始化新接受的连接*/
    void init( int sockfd, const sockaddr_in& addr );
    /*关闭连接*/
    void close_conn( bool real_close = true );
    /*处理客户请求*/
    void process();
    /*非阻塞读*/
    bool read();
    /*非阻塞写*/
    bool write();

private:
    /*初始化连接*/
    void init();
    /*解析http请求*/
    HTTP_CODE process_read();
    /*填充http请求*/
    bool process_write( HTTP_CODE ret );

    /*分析http请求*/
    HTTP_CODE parse_request_line( char* text );
    HTTP_CODE parse_headers( char* text );
    HTTP_CODE parse_content( char* text );
    HTTP_CODE do_request();
    char* get_line() { return m_read_buf + m_start_line; }
    LINE_STATUS parse_line();

    /*被process_write调用以填充HTTP应答*/
    void unmap();
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();

    void send_to_mycgi();

public:
    /*所有scoket上的事件都被注册到同一个epoll内核事件表中　所以将其设置为静态的*/
    static int m_epollfd;
    /*统计用户数量*/
    static int m_user_count;

private:
    /*该连接的socket和地址*/
    int m_sockfd;
    sockaddr_in m_address;

    /*初始化cgi*/
    int cgi = 1;

    /*读缓冲区*/
    char m_read_buf[ READ_BUFFER_SIZE ];
    /*标识度缓冲区中已经读入的客户数据的最后一个字节的的下一个位置*/
    int m_read_idx;
    /*当前正在分析的字符在读缓冲区的位置*/
    int m_checked_idx;
    /*当前正在解析的行的起始位置*/
    int m_start_line;
    /*写缓冲区*/
    char m_write_buf[ WRITE_BUFFER_SIZE ];
    int m_write_idx;

    CHECK_STATE m_check_state;
    /*请求方法*/
    METHOD m_method;

    /*客户请求的目标文件的完整路径其内容等于doc_root + m_url, doc_root是网站根目录*/
    char m_real_file[ FILENAME_LEN ];
    /*客户请求的目标文件名*/
    char* m_url;
    char* m_version;
    /*主机名*/
    char* m_host;
    int m_content_length;
    /*请求是否要保持连接*/
    bool m_linger;

    /*客户请求的目标文件被mmap到内存的起始位置*/
    char* m_file_address;
    /*目标文件的状态*/
    struct stat m_file_stat;
    /*writev执行写操作 便于集中写*/
    struct iovec m_iv[2];
    /*数量*/
    int m_iv_count;
};

#endif