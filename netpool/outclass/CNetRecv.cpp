

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

CNetRecv::CNetRecv(char *ipstr, uint16_t port, int fd) throw(std::runtime_error)
{
    if (-1 == init_peer_ipinfo(ipstr, port))
    {
        throw(std::runtime_error("Invalid server ip"));
    }

    m_fd = fd;
    init_common_data();
    _LOG_DEBUG("construct NetRecv(%s/%u), fd %d", m_ipstr, m_port, m_fd);
}

CNetRecv::CNetRecv(uint32_t ipaddr, uint16_t port, int fd) throw(std::runtime_error)
{
    if (-1 == init_peer_ipinfo(ipaddr, port))
    {
        throw(std::runtime_error("Invalid server ip"));
    }

    m_fd = fd;
    init_common_data();
    _LOG_DEBUG("construct1 NetRecv(%s/%u), fd %d", m_ipstr, m_port, m_fd);
}

CNetRecv::CNetRecv(uint32_t ipaddr, uint16_t port) throw(std::runtime_error)
{
    if (-1 == init_peer_ipinfo(ipaddr, port))
    {
        throw(std::runtime_error("Invalid server ip"));
    }

    m_fd = -1;
    init_common_data();
    _LOG_DEBUG("construct2 NetRecv(%s/%u)", m_ipstr, m_port, m_fd);
}


CNetRecv::CNetRecv(char *ipstr, uint16_t port) throw(std::runtime_error)
{
    if (-1 == init_peer_ipinfo(ipstr, port))
    {
        throw(std::runtime_error("Invalid server ip"));
    }

    m_fd = -1;
    init_common_data();
    _LOG_DEBUG("construct3 NetRecv(%s/%u)", m_ipstr, m_port, m_fd);
}


CNetRecv::~CNetRecv()
{
    destroy_common_data();
    _LOG_DEBUG("destruct NetRecv");
}

void CNetRecv::init_common_data()
{
    m_thrd_index = 0;

    m_is_register_write = false;
    m_is_register_read = false;
    m_is_register_connect = false;

    m_new_mem_func = NULL;
    m_free_mem_func = NULL;
    m_is_async_write = false;
#ifdef _WIN32
	m_queue_lock = 0;
#else
    pthread_spin_init(&m_queue_lock, 0);
#endif

    m_is_write_failed = false;

    MUTEX_SETUP(m_register_lock);
    m_is_freed = false;
}

void CNetRecv::destroy_common_data()
{
#ifdef _WIN32
	m_queue_lock = 0;
#else
	pthread_spin_destroy(&m_queue_lock);
#endif
    
    MUTEX_CLEANUP(m_register_lock);
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
    _LOG_ERROR("(%s/%u) fd %d default connect handle, shouldn't be here", m_ipstr, m_port, m_fd);
    return -1;
}

void CNetRecv::free_handle()
{
    /*default, delete self*/
    delete this;
}

void CNetRecv::_send_callback(int  fd, void* param1)
{
    CNetRecv *recvObj = (CNetRecv*)param1;

    if (recvObj->m_fd != fd)
    {
        _LOG_ERROR("(%s/%u) fd %d invalid fd when recv", recvObj->m_ipstr, recvObj->m_port, fd);
        recvObj->free();
        return;
    }

    if (-1 == recvObj->send_handle())
    {
        _LOG_ERROR("(%s/%u) fd %d send handle failed", recvObj->m_ipstr, recvObj->m_port, fd);
        recvObj->free();
        return;
    }

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

    recvObj->free_async_write_resource();
#ifndef _WIN32
	close(recvObj->m_fd);
#else
	closesocket(recvObj->m_fd);
#endif
    recvObj->m_fd = -1;

    /*�����ϲ���ͷź���, �û����Լ���freehandle��delete self*/
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


    ret = getsockopt(recvObj->m_fd, SOL_SOCKET, SO_ERROR, (char*)&err, &err_len);
    if (err == 0 && ret == 0)
    {
        _LOG_INFO("(%s/%u) fd %d connected.", recvObj->m_ipstr, recvObj->m_port, recvObj->m_fd);
        recvObj->connect_handle(true);
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
    }
#endif

    /*unregister connect event if no write event register*/
    if (false == recvObj->m_is_register_write
        && true == recvObj->m_is_register_connect)
    {
        recvObj->unregister_connect();
    }
    return;
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

    m_is_register_read = true;
    return 0;
}

void CNetRecv::unregister_read()
{
    np_del_read_job(m_fd, CNetRecv::_free_callback);
    m_is_register_read = false;
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

    m_is_register_connect = true;
    if(false == np_add_write_job(CNetRecv::_connect_callback, m_fd, (void*)this, m_thrd_index))
    {
        return -1;
    }

    return 0;
}

void CNetRecv::unregister_connect()
{
    np_del_write_job(m_fd, CNetRecv::_free_callback);
    m_is_register_connect = false;
}

int CNetRecv::register_write()
{
    if(false == np_add_write_job(CNetRecv::_send_callback, m_fd, (void*)this, m_thrd_index))
    {
        return -1;
    }

    m_is_register_write = true;
    return 0;
}

void CNetRecv::unregister_write()
{
    np_del_write_job(m_fd, CNetRecv::_free_callback);
    m_is_register_write = false;
}


void CNetRecv::init_async_write_resource(void* (*new_mem_func)(),
            void (*free_mem_func)(void*))
{
    m_is_write_failed = false;
    m_is_async_write = true;
    SQUEUE_INIT_HEAD(&m_data_queue);
    m_cur_send_node = NULL;

    m_new_mem_func = new_mem_func;
    m_free_mem_func = free_mem_func;
}

void CNetRecv::free_async_write_resource()
{
#ifdef _WIN32
	while (InterlockedExchange(&m_queue_lock, 1) == 1){
		sleep_s(0);
	}
#else
	pthread_spin_lock(&m_queue_lock);
#endif

    if (m_is_async_write)
    {
        /*�п��������̻߳�д����, �������󲻻��ڳ���write�¼�,��˲���ȡ����*/
        m_is_async_write = false;

        int spare_len = 0;
        if (m_is_write_failed == false)
        {
            /*�ȱ�֤�������������buf������*/
            if (m_cur_send_node != NULL)
            {
                spare_len = m_cur_send_node->produce_pos - m_cur_send_node->consume_pos;
                if ( spare_len != sock_write_timeout(m_fd, 
                                        &m_cur_send_node->data[m_cur_send_node->consume_pos], 
                                        spare_len, 
                                        DEF_WR_TIMEOUT))
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

                    m_free_mem_func(m_cur_send_node);
                    m_cur_send_node = NULL;
                    goto free_nodes;
                }
                else
                {
                    m_free_mem_func(m_cur_send_node);
                    m_cur_send_node = NULL;
                }
            }

            buf_node_t* buf_node = NULL;
            while((buf_node = (buf_node_t*)squeue_deq(&m_data_queue)) != NULL)
            {
                spare_len = buf_node->produce_pos - buf_node->consume_pos;
                if ( spare_len != sock_write_timeout(m_fd, 
                                        &buf_node->data[buf_node->consume_pos], 
                                        spare_len, 
                                        DEF_WR_TIMEOUT))
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
                    m_free_mem_func((void*)buf_node);
                    goto free_nodes;
                }
                else
                {
                    m_free_mem_func((void*)buf_node);
                }
            }
        }

free_nodes:            
        /*free all memory in queue*/
        int count = 0;
        struct list_head* queue_node = NULL;
        while((queue_node = squeue_deq(&m_data_queue)) != NULL)
        {
            m_free_mem_func(queue_node);
            count++;
        }

        if (m_cur_send_node != NULL)
        {
            m_free_mem_func(m_cur_send_node);
            count++;
        }    

        if (count > 0)
        {
            _LOG_ERROR("(peer %s/%u, local %s/%u) fd %d has %d uncompleted write node for write failed.", 
                m_ipstr, m_port, 
                m_local_ipstr,  m_local_port, m_fd, count);
        }
    }

#ifdef _WIN32
	InterlockedExchange(&m_queue_lock, 0);
#else
	pthread_spin_unlock(&m_queue_lock);
#endif
}

int CNetRecv::send_buf_node(buf_node_t *buf_node)
{  
    int spare_len = buf_node->produce_pos - buf_node->consume_pos;
    int send_len = 0;
    int total_send_len = 0;

    while(spare_len > 0)
    {
        send_len = send(m_fd, &buf_node->data[buf_node->consume_pos], spare_len, 0);
        if (send_len < 0)
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
            {
                break;
            }
            else if (errno == EINTR)
            { 
                _LOG_WARN("EINTR occured, continue write");
                continue;
            }
            else
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

                /*����-1��ֱ��free, ����recv����ʧ��*/
                return -1;
            }
        }
        else
        {
            assert(spare_len >= send_len);
            spare_len -= send_len;
            buf_node->consume_pos += send_len;
            total_send_len += send_len;
        }
    }

    return total_send_len;
}

int CNetRecv::send_handle()
{
    int count = 0;
    uint32_t bytes = 0;
    int ret = 0;

    if (m_cur_send_node == NULL)
    {
        /*û�нڵ�, ȡһ��*/
#ifdef _WIN32
		while (InterlockedExchange(&m_queue_lock, 1) == 1){
			sleep_s(0);
		}
#else
        pthread_spin_lock(&m_queue_lock);
#endif

        m_cur_send_node = (buf_node_t*)squeue_deq(&m_data_queue);

#ifdef _WIN32
		InterlockedExchange(&m_queue_lock,0);
#else
        pthread_spin_unlock(&m_queue_lock);
#endif
    }

    while(m_cur_send_node != NULL)
    {
        ret = send_buf_node(m_cur_send_node);
        if (-1 == ret)
        {
            /*�����Ƿ���ʧ��, ��ʧ��, free callback��ֱ���ͷ��ڴ�, �����ȱ�֤�����ڴ淢����*/
            m_is_write_failed = true;
            return -1;
        }

        bytes += ret;
        count++;

        if (m_cur_send_node->consume_pos == m_cur_send_node->produce_pos)
        {
            /*��buffer�ڵ��Ѿ�д��, �ͷ�*/
            m_free_mem_func(m_cur_send_node);
            m_cur_send_node = NULL;

            /*��ȡһ���ڵ�*/
#ifdef _WIN32
			while (InterlockedExchange(&m_queue_lock, 1) == 1){
				sleep_s(0);
			}
#else
			pthread_spin_lock(&m_queue_lock);
#endif
            m_cur_send_node = (buf_node_t*)squeue_deq(&m_data_queue);

#ifdef _WIN32
			InterlockedExchange(&m_queue_lock, 0);
#else
			pthread_spin_unlock(&m_queue_lock);
#endif
        }
        else
        {
            /*û������, �¸���д�¼�ʱ�ٷ�*/
            _LOG_DEBUG("(peer %s/%u, local %s/%u) fd %d not send finished, wait next.", 
                    m_ipstr, m_port, 
                    m_local_ipstr,  m_local_port, m_fd);
            this->register_write();
            break;
        }
    }

    if (count > 0)
    {
        _LOG_DEBUG("(peer %s/%u, local %s/%u) fd %d async write %d node, bytes %u.", 
            m_ipstr, m_port, 
            m_local_ipstr,  m_local_port, m_fd, count, bytes);
    }
    else if (bytes > 0)
    {
        _LOG_DEBUG("(peer %s/%u, local %s/%u) fd %d async write %d node, bytes %u.", 
            m_ipstr, m_port, 
            m_local_ipstr,  m_local_port, m_fd, count, bytes);
    }

    return 0;
}

int CNetRecv::send_data(char *buf, int buf_len)
{
#ifdef _WIN32
	while (InterlockedExchange(&m_queue_lock, 1) == 1){
		sleep_s(0);
	}
#else
	pthread_spin_lock(&m_queue_lock);
#endif
    if (m_is_async_write == false)
    {
#ifdef _WIN32
		InterlockedExchange(&m_queue_lock, 0);
#else
		pthread_spin_unlock(&m_queue_lock);
#endif

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

    buf_node_t *buf_node = NULL;
    buf_node = (buf_node_t*)squeue_get_tail(&m_data_queue);
    if (NULL == buf_node)
    {
        /*����һ���ڵ�*/
        buf_node = (buf_node_t*)m_new_mem_func();
        if (NULL == buf_node)
        {
#ifdef _WIN32
			InterlockedExchange(&m_queue_lock, 0);
#else
			pthread_spin_unlock(&m_queue_lock);
#endif
            _LOG_ERROR("send to (%s/%u) failed, fd %d, no buffer.", m_ipstr, m_port, m_fd);
            return -1;
        }
        SQUEUE_INIT_NODE(&buf_node->node);
        buf_node->produce_pos = 0;
        buf_node->consume_pos = 0;

        /*���뵽queue��*/
        squeue_inq((struct list_head*)(buf_node), &m_data_queue);
    }

    /*����д��*/
    int32_t fill_len = 0;
    while(fill_len < buf_len)
    {
        uint32_t buf_spare_len = BUF_NODE_SIZE - buf_node->produce_pos;
        uint32_t send_spare_len = buf_len - fill_len;
        if (send_spare_len > buf_spare_len)
        {
            memcpy(&buf_node->data[buf_node->produce_pos], &buf[fill_len], buf_spare_len);            
            buf_node->produce_pos += buf_spare_len;
            fill_len += buf_spare_len;

            /*�ٷ���һ���ڵ�*/
            buf_node = (buf_node_t*)m_new_mem_func();
            if (NULL == buf_node)
            {
#ifdef _WIN32
				InterlockedExchange(&m_queue_lock, 0);
#else
				pthread_spin_unlock(&m_queue_lock);
#endif
                _LOG_ERROR("send to (%s/%u) failed, fd %d, no buffer.", m_ipstr, m_port, m_fd);
                return -1;
            }

            SQUEUE_INIT_NODE(&buf_node->node);
            buf_node->produce_pos = 0;
            buf_node->consume_pos = 0;

            /*���뵽queue��*/
            squeue_inq((struct list_head*)(buf_node), &m_data_queue);
        }
        else
        {
            memcpy(&buf_node->data[buf_node->produce_pos], &buf[fill_len], send_spare_len);
            buf_node->produce_pos += send_spare_len;
            break;
        }
    }

#ifdef _WIN32
	InterlockedExchange(&m_queue_lock, 0);
#else
	pthread_spin_unlock(&m_queue_lock);
#endif

    /*maybe other thread call free at the same time, cause m_fd register on write, and this obj
        be deleted*/
    int ret = 0;
    MUTEX_LOCK(m_register_lock);
    if(m_is_freed)
    {
        MUTEX_UNLOCK(m_register_lock);
        return -1;
    }
    ret = this->register_write();
    MUTEX_UNLOCK(m_register_lock);

    return ret;
}

void CNetRecv::set_thrd_index(unsigned int thrd_index)
{
    /*���Զ�����ʹ���߳���Դ*/
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
    MUTEX_LOCK(m_register_lock);
    m_is_freed = true;

    if (m_is_register_read)
    {
        unregister_read();
    }

    if (m_is_register_write)
    {
        unregister_write();
    }
    else if (m_is_register_connect)
    {
        unregister_connect();
    }

    MUTEX_UNLOCK(m_register_lock);
}
