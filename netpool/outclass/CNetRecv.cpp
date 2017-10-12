

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
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

#include "utilstr.h"
#include "netpool.h"
#include "engine_ip.h"
#include "socketwrap.h"
#include "CNetRecv.h"


#define DEF_WR_TIMEOUT -1
//#define DEF_RD_TIMEOUT 3


CNetRecv::CNetRecv(char *ipstr, uint16_t port, int fd) throw(std::runtime_error)
{
    init_common_data();

    if (-1 == init_peer_ipinfo(ipstr, port))
    {
        throw(std::runtime_error("Invalid server ip"));
    }

    m_fd = fd;    
    _LOG_DEBUG("construct NetRecv(%s/%u), fd %d", m_ipstr, m_port, m_fd);
}

CNetRecv::CNetRecv(uint32_t ipaddr, uint16_t port, int fd) throw(std::runtime_error)
{
    init_common_data();

    if (-1 == init_peer_ipinfo(ipaddr, port))
    {
        throw(std::runtime_error("Invalid server ip"));
    }

    m_fd = fd;
    _LOG_DEBUG("construct1 NetRecv(%s/%u), fd %d", m_ipstr, m_port, m_fd);
}

CNetRecv::CNetRecv(uint32_t ipaddr, uint16_t port) throw(std::runtime_error)
{
    init_common_data();

    if (-1 == init_peer_ipinfo(ipaddr, port))
    {
        throw(std::runtime_error("Invalid server ip"));
    }

    m_fd = -1;
    _LOG_DEBUG("construct2 NetRecv(%s/%u)", m_ipstr, m_port, m_fd);
}


CNetRecv::CNetRecv(char *ipstr, uint16_t port) throw(std::runtime_error)
{
    init_common_data();

    if (-1 == init_peer_ipinfo(ipstr, port))
    {
        throw(std::runtime_error("Invalid server ip"));
    }

    m_fd = -1;
    _LOG_DEBUG("construct3 NetRecv(%s/%u)", m_ipstr, m_port, m_fd);
}


CNetRecv::~CNetRecv()
{
    destroy_common_data();
    _LOG_DEBUG("destruct NetRecv");
}

void CNetRecv::init_common_data()
{
    m_is_connected = false;
    m_is_pause_read = false;
    m_is_fwd_server = true;

    m_ipaddr = 0;
    memset(m_ipstr, 0, sizeof(m_ipstr));
    m_port = 0;

    m_local_ipaddr = 0;
    memset(m_local_ipstr, 0, sizeof(m_local_ipstr));
    m_local_port = 0;

    m_send_q_busy_cnt = 8;

    m_thrd_index = 0;
    m_is_async_write = true;
    m_is_freeing = false;

    m_is_register_write = false;
    MUTEX_SETUP(m_free_lock);
}

void CNetRecv::destroy_common_data()
{
    if (m_send_q.node_cnt() > 0)
    {
        _LOG_WARN("(%s/%u) fd %d has %d uncompleted write node when destruct", m_ipstr, m_port, m_fd,
            m_send_q.node_cnt());
        m_send_q.clean_q();
    }
    MUTEX_CLEANUP(m_free_lock);
}

int CNetRecv::init_peer_ipinfo(uint32_t ipaddr, uint16_t port)
{
    m_ipaddr = ipaddr;
    m_port = port;

	if (NULL == engine_ipv4_to_str(htonl(ipaddr), m_ipstr))
    {
        _LOG_ERROR("ip to str failed.");
        return -1;
    }
	m_ipstr[IP_DESC_LEN] = 0;
    return 0;
}

int CNetRecv::init_peer_ipinfo(char *ipstr, uint16_t port)
{
	util_strncpy(m_ipstr, ipstr, IP_DESC_LEN);
	m_ipstr[IP_DESC_LEN] = 0;
    m_port = port;

	if (0 != engine_str_to_ipv4(ipstr, &m_ipaddr))
    {
        _LOG_ERROR("Invalid server %s", ipstr);
        return -1;
    }
	m_ipaddr = htonl(m_ipaddr);
    return 0;
}

int CNetRecv::init_local_ipinfo()
{
    struct sockaddr_in name;
    socklen_t len = sizeof (struct sockaddr);  
    if (getsockname (m_fd, (struct sockaddr *)&name, &len) == -1) {
        return -1;
    }     
    m_local_ipaddr = ntohl(name.sin_addr.s_addr);
    m_local_port = ntohs(name.sin_port);

	if (NULL == engine_ipv4_to_str(htonl(m_local_ipaddr), m_local_ipstr))
	{
		_LOG_ERROR("ip to str failed.");
		return -1;
	}
	m_local_ipstr[IP_DESC_LEN] = 0;

    if (m_local_ipstr[0] == 0)
    {
        strncpy(m_local_ipstr, "0.0.0.0", IP_DESC_LEN);
    }
    
    _LOG_DEBUG("(%s/%u) fd %d get local ip : %s/%u", m_ipstr, m_port, m_fd, m_local_ipstr, m_local_port);
    return 0;
}

int CNetRecv::connect_handle(BOOL result)
{
    return 0;
}

int CNetRecv::send_pre_handle()
{
    return 0;
}

int CNetRecv::send_post_handle()
{
    if (m_is_fwd_server == false)
    {
        MUTEX_LOCK(this->m_free_lock);
        if (m_is_pause_read)
        {
            this->resume_read();
        }
        MUTEX_UNLOCK(this->m_free_lock);
    }
    
    return 0;
}

void CNetRecv::free_handle()
{
    /*default, delete self*/
    delete this;
}

void CNetRecv::_send_callback(int  fd, void* param1)
{
    CNetRecv *recvObj = (CNetRecv*)param1;
    int ret = 0;

    if (recvObj->m_fd != fd)
    {
        recvObj->m_send_q.clean_q();
        _LOG_ERROR("(%s/%u) fd %d invalid fd when recv", recvObj->m_ipstr, recvObj->m_port, fd);
        goto exitAndTryfreeSelf;
    }

    if (-1 == recvObj->send_pre_handle())
    {
        recvObj->m_send_q.clean_q();
        _LOG_ERROR("(%s/%u) fd %d pre-send handle failed", recvObj->m_ipstr, recvObj->m_port, fd);
        goto exitAndTryfreeSelf;
    }

    ret = recvObj->m_send_q.consume_q(recvObj->m_fd);
    if (ret < 0)
    {
        /*send failed*/
        recvObj->m_send_q.clean_q();
        goto exitAndTryfreeSelf;
    }

    if (recvObj->m_send_q.node_cnt() < recvObj->m_send_q_busy_cnt)
    {
        if (-1 == recvObj->send_post_handle())
        {
            recvObj->m_send_q.clean_q();
            _LOG_ERROR("(%s/%u) fd %d post-send handle failed", recvObj->m_ipstr, recvObj->m_port, fd);
            goto exitAndTryfreeSelf;
        }
    }

    if(recvObj->m_send_q.node_cnt() > 0)
    {
        /*not send completed, wait next write evt*/
        if(0 != recvObj->register_write())
        {
            recvObj->m_send_q.clean_q();
            goto exitAndTryfreeSelf;
        }
        /*wait next event*/
        return;
    }
    
    /*send completed*/
exitAndTryfreeSelf:
    /*all data has send finished, free it*/
    MUTEX_LOCK(recvObj->m_free_lock);
    if (recvObj->m_is_freeing)
    {
        np_del_io_job(recvObj->m_fd, CNetRecv::_free_callback);
        _LOG_INFO("del job after send end");
    }
    MUTEX_UNLOCK(recvObj->m_free_lock);
    return;
}

void CNetRecv::_recv_callback(int  fd, void* param1, char *recvBuf, int recvLen)
{
    CNetRecv *recvObj = (CNetRecv*)param1;

    if (recvObj->m_fd != fd)
    {
        _LOG_ERROR("(%s/%u) fd %d invalid fd when recv", recvObj->m_ipstr, recvObj->m_port, fd);
        recvObj->free();
        return;
    }

    if (recvLen == 0)
    {
        _LOG_INFO("(peer %s/%u, local %s/%u) fd %d close.", recvObj->m_ipstr, recvObj->m_port, 
            recvObj->m_local_ipstr, recvObj->m_local_port, fd);
        recvObj->free();
        return;
    }

    if (-1 == recvObj->recv_handle(recvBuf, recvLen))
    {
        _LOG_ERROR("(%s/%u) fd %d recv handle failed", recvObj->m_ipstr, recvObj->m_port, fd);
        recvObj->free();
        return;
    }

    return;
}

void CNetRecv::_free_callback(int  fd, void* param1)
{
    CNetRecv *recvObj = (CNetRecv*)param1;
    if (recvObj->m_fd != fd)
    {
        _LOG_ERROR("(%s/%u) fd %d invalid when free", recvObj->m_ipstr, recvObj->m_port, fd);
        recvObj->free();
        return;
    }
    
    _LOG_INFO("(peer %s/%u local %s/%u) fd %d call free callback", recvObj->m_ipstr, recvObj->m_port, 
        recvObj->m_local_ipstr, recvObj->m_local_port,
        recvObj->m_fd);

#ifndef _WIN32
	close(recvObj->m_fd);
#else
	closesocket(recvObj->m_fd);
#endif
    recvObj->m_fd = -1;

    /*调用上层的释放函数, 用户在自己的freehandle中delete self*/
    recvObj->free_handle();
}


void CNetRecv::_connect_callback(int  fd, void* param1)
{
    CNetRecv *recvObj = (CNetRecv*)param1;

    if (recvObj->m_fd != fd)
    {
        _LOG_ERROR("(%s/%u) fd %d invalid fd when connect, wanted %d", 
            recvObj->m_ipstr, recvObj->m_port, fd, recvObj->m_fd);
        recvObj->free();
        return;
    }

    int ret = 0;
    int err = 0;
    socklen_t err_len = sizeof (err);
    
#if 0
    int sock_opt = 0;
    int optlen = 0;

    if ((getsockopt(recvObj->m_fd, SOL_SOCKET, SO_RCVLOWAT, (char*)&sock_opt, (int*)&optlen)) == -1)
    {
#ifndef _WIN32
        _LOG_ERROR("getsockopt failed, %s\n", strerror(errno));
#else
        _LOG_ERROR("getsockopt failed, %d\n", WSAGetLastError());
#endif        
    }
    _LOG_WARN("test1, opt %d", sock_opt);

    sock_opt=100;
    if ((setsockopt(recvObj->m_fd, SOL_SOCKET, SO_RCVLOWAT, (char*)&sock_opt, (int)sizeof(sock_opt))) == -1)
    {
#ifndef _WIN32
        _LOG_ERROR("setsockopt failed, %s\n", strerror(errno));
#else
        _LOG_ERROR("setsockopt failed, %d\n", WSAGetLastError());
#endif        
    }

    sock_opt = 0;
    if ((getsockopt(recvObj->m_fd, SOL_SOCKET, SO_RCVLOWAT, (char*)&sock_opt, (int*)&optlen)) == -1)
    {
#ifndef _WIN32
        _LOG_ERROR("getsockopt failed, %s\n", strerror(errno));
#else
        _LOG_ERROR("getsockopt failed, %d\n", WSAGetLastError());
#endif        
    }
    _LOG_WARN("test2, opt %d", sock_opt);
#endif

    ret = getsockopt(recvObj->m_fd, SOL_SOCKET, SO_ERROR, (char*)&err, &err_len);
    if (err == 0 && ret == 0)
    {
        _LOG_INFO("(%s/%u) fd %d connected.", recvObj->m_ipstr, recvObj->m_port, recvObj->m_fd);
        if(0 != recvObj->connect_handle(true))
        {
            recvObj->free();
            return;
        }

        recvObj->m_is_connected = true;
    }
    else
    {
        char err_buf[64] = {0};
#ifdef _WIN32
        _LOG_WARN("(%s/%u) fd %d connect failed, err:%d", 
            recvObj->m_ipstr, recvObj->m_port, recvObj->m_fd, WSAGetLastError());
#else
        _LOG_WARN("(%s/%u) fd %d connect failed, err:%s", 
            recvObj->m_ipstr, recvObj->m_port, recvObj->m_fd, str_error_s(err_buf, 32, err));
#endif
        recvObj->connect_handle(false);
        recvObj->free();
        return;
    }

    return;
}


int CNetRecv::pause_read()
{
    if(false == np_pause_read_on_job(m_fd))
    {
        return -1;
    }

    m_is_pause_read = true;
    return 0;
}

int CNetRecv::resume_read()
{
    if(false == np_resume_read_on_job(m_fd))
    {
        return -1;
    }

    m_is_pause_read = false;
    return 0;
}


int CNetRecv::register_read()
{
    if (m_fd == -1)
    {
        _LOG_ERROR("no fd when init NetRecv, %s/%u", m_ipstr, m_port);
        return -1;
    }
    
    /*initialize local ipaddr and port*/
    init_local_ipinfo();

    sock_set_unblock(m_fd);
    if(false == np_add_read_job(CNetRecv::_recv_callback, m_fd, (void*)this, m_thrd_index))
    {
        return -1;
    }

    return 0;
}

int CNetRecv::register_connect()
{
    if ((m_fd = (int)socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        _LOG_ERROR("create socket failed");
        return -1;
    }

    /*set connect timeout time*/
    struct timeval timeo = {8, 0};   
    socklen_t len = sizeof(timeo);  
    setsockopt(m_fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeo, len);  

    /*set to nonblock*/
    sock_set_unblock(m_fd);

    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(m_port);
    sa.sin_addr.s_addr = htonl(m_ipaddr); 
    if (connect(m_fd, (struct sockaddr*)&sa, sizeof(sa)) == 0) 
    {
        m_is_connected = true;
        this->connect_handle(true);
        return 0;
    }
    

#ifdef _WIN32
    int errno_ll = WSAGetLastError();
    if (errno_ll != WSAEWOULDBLOCK && errno_ll != WSAEALREADY)
    {
        _LOG_ERROR("connect failed, %s:%u %d", m_ipstr, m_port, WSAGetLastError());
        closesocket(m_fd);
        m_fd = -1;
		return -1;
    }
#else
    if (errno != EINPROGRESS)
    {
        _LOG_ERROR("connect failed, %s:%u %s", m_ipstr, m_port, strerror(errno));
        close(m_fd);
        m_fd = -1;
		return -1;
    }
#endif

    if(false == np_add_write_job(CNetRecv::_connect_callback, m_fd, (void*)this, m_thrd_index))
    {
        return -1;
    }

    return 0;
}

int CNetRecv::register_write()
{
    if(false == np_add_write_job(CNetRecv::_send_callback, m_fd, (void*)this, m_thrd_index))
    {
        /*if failed, clear send q*/
        return -1;
    }

    m_is_register_write = true;
    return 0;
}

void CNetRecv::set_async_write_flag(bool is_async)
{
    m_is_async_write = is_async;
}

int CNetRecv::send_data(char *buf, int buf_len)
{
    if (m_is_async_write == false)
    {
        if ( buf_len != sock_write_timeout(m_fd, buf, buf_len, DEF_WR_TIMEOUT))
        {
            char err_buf[64] = {0};
#ifdef _WIN32
            _LOG_ERROR("(peer %s/%u local %s/%u) fd %d send failed, %d.", 
                m_ipstr, m_port, m_local_ipstr, m_local_port, m_fd,
                WSAGetLastError());
#else
            _LOG_ERROR("(peer %s/%u local %s/%u) fd %d send failed, %s.", 
                m_ipstr, m_port, m_local_ipstr, m_local_port, m_fd,
                str_error_s(err_buf, 32, errno));
#endif
            return -1;
        }
        return 0;
    }

    if(0 != m_send_q.produce_q(buf, buf_len))
    {
        _LOG_ERROR("(peer %s/%u local %s/%u) fd %d produce to queue failed.", 
            m_ipstr, m_port, m_local_ipstr, m_local_port, m_fd);
        return -1;
    }

    int ret = 0;
    MUTEX_LOCK(m_free_lock);
    if (m_is_freeing)
    {
        MUTEX_UNLOCK(m_free_lock);
        _LOG_WARN("(peer %s/%u local %s/%u) fd %d produce to queue failed, is in freeing.", 
            m_ipstr, m_port, m_local_ipstr, m_local_port, m_fd);
        return -1;
    }

    if (m_is_fwd_server == false)
    {
        if (m_send_q.node_cnt() >= m_send_q_busy_cnt)
        {
            this->pause_read();
        }
    }

    ret = this->register_write();
    MUTEX_UNLOCK(m_free_lock);

    return ret;
}

void CNetRecv::set_thrd_index(unsigned int thrd_index)
{
    /*都自动均衡使用线程资源*/
    //m_thrd_index = thrd_index;
}

BOOL CNetRecv::is_connected()
{
    return m_is_connected;
}  

int CNetRecv::init()
{
    if (-1 != m_fd)
    {
        return register_read();
    }

    /*need to connect peer*/
    return register_connect();
}

void CNetRecv::free()
{
    MUTEX_LOCK(m_free_lock);
    m_is_freeing = true;

    /*if has data not send completed, wait*/
    if (m_send_q.node_cnt() == 0)
    {
        np_del_io_job(this->m_fd, CNetRecv::_free_callback);
    }

    MUTEX_UNLOCK(m_free_lock);
}
