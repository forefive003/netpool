#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <errno.h>
#include <signal.h>

#ifdef _WIN32
#include <Ws2tcpip.h>
#include <windows.h>
#include <process.h>
#else
#include <sys/epoll.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#endif

#include "netpool.h"
#include "socketwrap.h"
#include "CThread.h"
#include "CThreadPool.h"
#include "CJobIo.h"
#include "CThrdComServ.h"
#include "CNetPoll.h"
#include "thrdComm.h"
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
#ifndef _WIN32
        close(s);
#else
		closesocket(s);
#endif
        _LOG_ERROR("connect to thread %d comm server failed, dst port %d", m_thrd_index, m_thrd_dst_port);
        return -1;
    }

    int errno_ll = 0;
    int ret = 0;
    /*send message*/
    ret = send(s, (char*)&m_thrd_index, sizeof(m_thrd_index), 0);
    if (sizeof(m_thrd_index) != ret)
    {        
#ifdef _WIN32
		closesocket(s);
        errno_ll = WSAGetLastError();
#else
		close(s);
        errno_ll = errno;
#endif      
        _LOG_ERROR("send thrd index to thread %d comm server failed, dst port %d, errno %d", 
            m_thrd_index, m_thrd_dst_port, errno_ll);
        return -1;
    }

    ret = send(s, (char*)&type, sizeof(type), 0);
    if (sizeof(type) != ret)
    {        
#ifdef _WIN32
		closesocket(s);
        errno_ll = WSAGetLastError();
#else
		close(s);
        errno_ll = errno;
#endif      
        _LOG_ERROR("send msg type to thread %d comm server failed, dst port %d, errno %d", 
            m_thrd_index, m_thrd_dst_port, errno_ll);
        return -1;
    }

    ret = send(s, (char*)buffer, buffer_len, 0);
    if (buffer_len != ret)
    {
#ifdef _WIN32
		closesocket(s);
        errno_ll = WSAGetLastError();
#else
		close(s);
        errno_ll = errno;
#endif      
        _LOG_ERROR("send msf buffer to thread %d comm server failed, dst port %d, errno %d", 
            m_thrd_index, m_thrd_dst_port, errno_ll);
        return -1;
    }
#ifdef _WIN32
	closesocket(s);
#else
    close(s);
#endif
    return 0;
}

int CThrdComServ::init()
{
    m_listen_fd = sock_create_server(m_local_ipstr, m_listen_port);
    if (-1 == m_listen_fd)
    {
        _LOG_INFO("failed to listen on port %d, local %s", m_listen_port, m_local_ipstr);
        return -1;
    }

    struct sockaddr_in thrd_addr;
    socklen_t namelen = sizeof(sockaddr_in);
    memset(&thrd_addr, 0, sizeof(thrd_addr));

    if (getsockname(m_listen_fd, (sockaddr*)&thrd_addr, &namelen) == -1)
    {  
        unregister_accept();
        _LOG_ERROR("get thread message port failed, thrd index %d.", m_thrd_index);
        return -1;
    }

    m_thrd_dst_port = ntohs(thrd_addr.sin_port);
    _LOG_INFO("thread %d comm server listen on port %u, fd %d", m_thrd_index, m_thrd_dst_port, m_listen_fd);

    sock_set_unblock(m_listen_fd);
    np_add_listen_job(CNetAccept::_accept_callback, m_listen_fd, (void*)this, m_thrd_index);
    return 0;
}
