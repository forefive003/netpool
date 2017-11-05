#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

#ifdef _WIN32
//resolve question:
//c : \program files\microsoft sdks\windows\v6.0a\include\ws2def.h(91) : warning C4005 : 'AF_IPX' : macro redefinition
//	1>        c:\program files\microsoft sdks\windows\v6.0a\include\winsock.h(460) : see previous definition of 'AF_IPX'
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
#include "CSocksSendQ.h"
#include "CSocksMem.h"

int CSocksSendQ::produce_q(char *buf, int buf_len)
{
	int ret = 0;
    int32_t fill_len = 0;

    this->lock();

    buf_node_t *buf_node = NULL;
    buf_node = (buf_node_t*)squeue_get_tail(&m_data_queue);
    if (NULL == buf_node)
    {
        /*分配一个节点*/
        buf_node = (buf_node_t*)socks_malloc();
        if (NULL == buf_node)
        {
            _LOG_ERROR("socks malloc no buffer.");
            ret = -1;
            goto exitHere;
        }
        SQUEUE_INIT_NODE(&buf_node->node);
        buf_node->produce_pos = 0;
        buf_node->consume_pos = 0;

        /*插入到queue中*/
        squeue_inq((struct list_head*)(buf_node), &m_data_queue);
    }

    /*数据写入*/
    while(fill_len < buf_len)
    {
        uint32_t buf_spare_len = BUF_NODE_SIZE - buf_node->produce_pos;
        uint32_t send_spare_len = buf_len - fill_len;
        if (send_spare_len > buf_spare_len)
        {
            memcpy(&buf_node->data[buf_node->produce_pos], &buf[fill_len], buf_spare_len);            
            buf_node->produce_pos += buf_spare_len;
            fill_len += buf_spare_len;

            /*再分配一个节点*/
            buf_node = (buf_node_t*)socks_malloc();
            if (NULL == buf_node)
            {
                _LOG_ERROR("socks malloc no buffer.");
                ret = -1;
            	goto exitHere;
            }

            SQUEUE_INIT_NODE(&buf_node->node);
            buf_node->produce_pos = 0;
            buf_node->consume_pos = 0;

            /*插入到queue中*/
            squeue_inq((struct list_head*)(buf_node), &m_data_queue);
        }
        else
        {
            memcpy(&buf_node->data[buf_node->produce_pos], &buf[fill_len], send_spare_len);
            buf_node->produce_pos += send_spare_len;
            break;
        }
    }

exitHere:
    this->unlock();
    return ret;
}


int CSocksSendQ::send_buf_node(int fd, buf_node_t *buf_node)
{  
    int spare_len = buf_node->produce_pos - buf_node->consume_pos;
    int send_len = 0;
    int total_send_len = 0;

    while(spare_len > 0)
    {
        send_len = send(fd, &buf_node->data[buf_node->consume_pos], spare_len, 0);
        if (send_len < 0)
        {
#ifdef _WIN32
            DWORD dwError = WSAGetLastError();
            if (dwError == WSAEINTR || dwError == WSAEWOULDBLOCK)
            {
                break;
            }
            else
            {
                _LOG_WARN("fd %d send failed, %d.", fd, dwError);
                return -1;
            }
#else
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
                _LOG_WARN("fd %d send failed, %s.", fd, str_error_s(err_buf, 32, errno));
                return -1;
            }
#endif
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

int CSocksSendQ::consume_q(int fd)
{
    int count = 0;
    uint32_t bytes = 0;
    int ret = 0;

    if (m_cur_send_node == NULL)
    {
        /*没有节点, 取一个*/
        this->lock();
        m_cur_send_node = (buf_node_t*)squeue_deq(&m_data_queue);
        this->unlock();
    }

    while(m_cur_send_node != NULL)
    {
        ret = this->send_buf_node(fd, m_cur_send_node);
        if (-1 == ret)
        {
            return -1;
        }

        bytes += ret;
        count++;

        if (m_cur_send_node->consume_pos == m_cur_send_node->produce_pos)
        {
            /*此buffer节点已经写完, 释放*/
            socks_free(m_cur_send_node);
            m_cur_send_node = NULL;

            /*再取一个节点*/
            this->lock();
            m_cur_send_node = (buf_node_t*)squeue_deq(&m_data_queue);
            this->unlock();
        }
        else
        {
            /*没发送完, 下个可写事件时再发*/
            return 1;
        }
    }

    if (count > 0)
    {
        _LOG_DEBUG("fd %d async write %d node, bytes %u.", fd, count, bytes);
    }

    /*发送完成*/
    return 0;
}

void CSocksSendQ::clean_q()
{
    /*free all memory in queue*/
    int count = 0;
    struct list_head* queue_node = NULL;
    
    this->lock();
    while((queue_node = squeue_deq(&m_data_queue)) != NULL)
    {
        socks_free(queue_node);
        count++;
    }

    if (m_cur_send_node != NULL)
    {
        socks_free(m_cur_send_node);
        count++;
    }    
    this->unlock();
}

void CSocksSendQ::queue_cat(CSocksSendQ &qobj)
{
    struct list_head* queue_node = NULL;

    qobj.lock();
    
    while((queue_node = squeue_deq(&qobj.m_data_queue)) != NULL)
    {
        this->lock();
        squeue_inq(queue_node, &m_data_queue);
        this->unlock();
    }

    qobj.unlock();
}

unsigned int CSocksSendQ::node_cnt()
{
	return m_data_queue.count;
}
