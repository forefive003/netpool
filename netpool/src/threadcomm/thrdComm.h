#ifndef _THRD_COMM_H
#define _THRD_COMM_H

#include "netpool.h"

#define THRD_COMM_ADDR  0x7f000001
#define THRD_COMM_ADDR_STR "127.0.0.1"

typedef struct
{
	int fd;
	free_hdl_func free_func;
	int thrd_index;
}MSG_DEL_IO_JOB_T;

typedef struct 
{
	accept_hdl_func acpt_func;
	int fd;
	void *param1;
	int thrd_index;
}MSG_ADD_LISTEN_JOB_T;

typedef struct 
{
	int fd;
	free_hdl_func free_func;
	int thrd_index;
}MSG_DEL_LISTEN_JOB_T;

typedef struct 
{
	read_hdl_func read_func;
	int fd;
	void* param1;
	int thrd_index;
	int bufferSize;
	BOOL isTcp;
}MSG_ADD_READ_JOB_T;

typedef struct 
{
	int  fd;
	free_hdl_func free_func;
	int thrd_index;
}MSG_DEL_READ_JOB_T;

typedef struct 
{
	write_hdl_func io_func;
	int  fd;
	void* param1; 
	int thrd_index;
	BOOL isTcp;
}MSG_ADD_WRITE_JOB_T;

typedef struct 
{
	int  fd;
	free_hdl_func free_func;
	int thrd_index;
}MSG_DEL_WRITE_JOB_T;

typedef struct
{
	int fd;
	int thrd_index;
}MSG_PAUSE_READ_T;

typedef struct
{
	int fd;
	int thrd_index;
}MSG_RESUME_READ_T;

enum 
{
	MSG_DEL_IO_JOB = 1,
	MSG_ADD_LISTEN_JOB,
	MSG_DEL_LISTEN_JOB,
	MSG_ADD_READ_JOB,
	MSG_DEL_READ_JOB,
	MSG_ADD_WRITE_JOB,
	MSG_DEL_WRITE_JOB,
	MSG_PAUSE_READ,
	MSG_RESUME_READ,	
};

#endif
