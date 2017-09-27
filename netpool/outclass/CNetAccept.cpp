
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
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
#include "socketwrap.h"
#include "CNetAccept.h"

CNetAccept::CNetAccept(uint16_t port)
{
    m_listen_port = port;
    m_listen_fd = -1;
    _LOG_DEBUG("construct NetAccept on port %u", m_listen_port);
}

CNetAccept::~CNetAccept()
{
    _LOG_DEBUG("destruct NetAccept on port %u", m_listen_port);
}

void CNetAccept::_accept_callback(int  cli_fd, void* param1)
{
    CNetAccept *acptObj = (CNetAccept*)param1;

    uint32_t client_ipaddr = 0;
    uint16_t client_port = 0;

    struct sockaddr_in addrMy;
    memset(&addrMy,0,sizeof(addrMy));
    int len = sizeof(addrMy);

    if (getpeername(cli_fd,(sockaddr*)&addrMy, (socklen_t*)&len) != 0)
    {
#ifndef _WIN32
        _LOG_ERROR("getpeername Error, cli fd %d, error %s", cli_fd, strerror(errno));
        close(cli_fd);
#else
        _LOG_ERROR("getpeername Error, cli fd %d, error %d", cli_fd, WSAGetLastError());
		closesocket(cli_fd);        
#endif        
        return;
    }

    client_ipaddr = ntohl(addrMy.sin_addr.s_addr);
    client_port = ntohs(addrMy.sin_port);
    _LOG_DEBUG("accept new obj on port %u: 0x%x/%u, fd %u", acptObj->m_listen_port, client_ipaddr, client_port, cli_fd);
    
    if (0 != acptObj->accept_handle(cli_fd, client_ipaddr, client_port))
    {
        _LOG_ERROR("failed to handle accept");
#ifndef _WIN32
		close(cli_fd);
#else
		closesocket(cli_fd);
#endif
        return;
    }

    return;
}

void CNetAccept::free_handle()
{
    _LOG_INFO("default free listenObj(%u)", m_listen_port);
}

void CNetAccept::_free_callback(int  fd, void* param1)
{
    CNetAccept *acptObj = (CNetAccept*)param1;
    if (acptObj->m_listen_fd != fd)
    {
        _LOG_ERROR("listenObj(%u) fd %d invalid when free", acptObj->m_listen_port, fd);
        return;
    }
    
    _LOG_INFO("listenObj(%u) fd %d call free callback", acptObj->m_listen_port, fd);

    
    /*�����ϲ���ͷź���*/
    acptObj->free_handle();
#ifndef _WIN32
	close(acptObj->m_listen_fd);
#else
	closesocket(acptObj->m_listen_fd);
#endif
    acptObj->m_listen_fd = -1;
    delete acptObj;
}

int CNetAccept::register_accept()
{
    m_listen_fd = sock_create_server(m_listen_port);
    if (-1 == m_listen_fd)
    {
        _LOG_INFO("failed to listen on port %d", m_listen_port);
        return -1;
    }

    sock_set_unblock(m_listen_fd);
    np_add_listen_job(CNetAccept::_accept_callback, m_listen_fd, (void*)this);
    
    _LOG_INFO("listen on port %u, fd %d", m_listen_port, m_listen_fd);
    return 0;
}

void CNetAccept::unregister_accept()
{
    if (m_listen_fd != -1)
    {
        _LOG_INFO("stop to listen");
        np_del_listen_job(m_listen_fd, CNetAccept::_free_callback);
    }
}

int CNetAccept::init()
{
    return register_accept();
}

void CNetAccept::free()
{
    unregister_accept();
}
