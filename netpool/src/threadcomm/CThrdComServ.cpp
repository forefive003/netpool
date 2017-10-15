#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <errno.h>
#include <signal.h>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <sys/epoll.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#endif

#include "netpool.h"
#include "CThread.h"
#include "CThreadPool.h"
#include "CJobIo.h"
#include "CNetPoll.h"
#include "thrdComm.h"
#include "CThrdComServ.h"
#include "CThrdComObj.h"

int CThrdComServ::accept_handle(int conn_fd, uint32_t client_ip, uint16_t client_port)
{
    CThrdComObj *newCommObj = new CThrdComObj(m_thrd_index, client_ip, client_port, conn_fd);
    if(0 != newCommObj->init())
    {
        delete newCommObj;
        return -1;
    }

    return 0;
}

int CThrdComServ::send_comm_msg(int type, char *buffer, int buffer_len)
{
    int s;
    struct sockaddr_in sa;

    if ((s = (int)socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        _LOG_ERROR("socket failed");
        return -1;
    }

    sa.sin_family = AF_INET;
    sa.sin_port = htons(m_thrd_dst_port);
    sa.sin_addr.s_addr = htonl(THRD_COMM_ADDR);

    /* FIXME: it may block! */
    if (connect(s, (struct sockaddr*)&sa, sizeof(sa)) != 0) 
    {
        close(s);
        _LOG_ERROR("connect to thread %d comm server failed, dst port %d", m_thrd_index, m_thrd_dst_port);
        return -1;
    }

    int errno_ll = 0;
    int ret = 0;
    /*send message*/
    ret = send(s, (char*)&m_thrd_index, sizeof(m_thrd_index));
    if (sizeof(m_thrd_index) != ret)
    {
        close(s);
#ifdef _WIN32
        errno_ll = WSAGetLastError();
#else
        errno_ll = errno;
#endif      
        _LOG_ERROR("send thrd index to thread %d comm server failed, dst port %d, errno %d", 
            m_thrd_index, m_thrd_dst_port, errno_ll);
        return -1;
    }

    ret = send(s, (char*)&type, sizeof(type));
    if (sizeof(type) != ret)
    {
        close(s);
#ifdef _WIN32
        errno_ll = WSAGetLastError();
#else
        errno_ll = errno;
#endif      
        _LOG_ERROR("send msg type to thread %d comm server failed, dst port %d, errno %d", 
            m_thrd_index, m_thrd_dst_port, errno_ll);
        return -1;
    }

    ret = send(s, (char*)buffer, buffer_len);
    if (buffer_len != ret)
    {
        close(s);
#ifdef _WIN32
        errno_ll = WSAGetLastError();
#else
        errno_ll = errno;
#endif      
        _LOG_ERROR("send msf buffer to thread %d comm server failed, dst port %d, errno %d", 
            m_thrd_index, m_thrd_dst_port, errno_ll);
        return -1;
    }

    close(s);
    return 0;
}

int CThrdComServ::init(const char *local_ipstr, uint16_t port)
{
    m_listen_port = port;
    memset(m_local_ipstr, 0, sizeof(m_local_ipstr));
    strncpy(m_local_ipstr, local_ipstr, IP_DESC_LEN);
    
    if(0 != register_accept())
    {
        return -1;
    }

    int namelen = sizeof(sockaddr_in);
    if (getsockname(m_fd, (sockaddr*)&m_thrd_message_addr, &namelen) == -1)
    {  
        unregister_accept();
        _LOG_ERROR("get thread message port failed, thrd index %d.", m_thrd_index);
        return -1;
    }

    m_thrd_dst_port = m_thrd_message_addr.sin_port;
    _LOG_INFO("thread %d comm server listen on port %u", m_thrd_index, m_thrd_dst_port);
    return 0;
}
