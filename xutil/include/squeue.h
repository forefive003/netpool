
#ifndef _S_QUEUE_H
#define _S_QUEUE_H

#include "list.h"
#include "commtype.h"


#ifdef __cplusplus
extern "C"
{
#endif

typedef struct s_queue_head
{
	struct list_head *first;  /*ִ�е�һ���ڵ�*/
	struct list_head **last_addr; /*���һ���ڵ�ĵ�ַ*/
	unsigned int count;
}sq_head_t;

#define SQUEUE_EMPTY(head) (NULL == (head)->first)

#define SQUEUE_INIT_HEAD(ptr) do { \
    (ptr)->first = NULL;\
    (ptr)->last_addr = &(ptr)->first;\
    (ptr)->count=0;\
} while (0)

#define SQUEUE_INIT_NODE(ptr) do { \
    (ptr)->next = NULL;\
    (ptr)->prev = NULL;\
} while (0)

static inline void squeue_copy(sq_head_t *dst, sq_head_t *src)
{
	if (src->first)
	{
		/*��Ԫ�أ���Ԫ�ص�prev�޸�*/
		dst->first = src->first;
		dst->last_addr = src->last_addr;

		src->first->prev = (struct list_head *)&(dst->first);
	}
	else
	{
		/*�����ʼ��Ϊ�ձ�*/
		SQUEUE_INIT_HEAD(dst);
	}

	dst->count = src->count;
}

static inline void squeue_inq(struct list_head *elment, struct s_queue_head *head)
{
	/*���뵽β��*/
	elment->next = NULL;
	elment->prev = (struct list_head*)head->last_addr;
	*head->last_addr = elment;
	head->last_addr = &elment->next;
	head->count++;
}

static inline void squeue_insert_prev(struct s_queue_head *head,
								struct list_head *next_node,
								struct list_head *node)
{
	node->next = next_node;
	node->prev = next_node->prev;
    
    struct list_head *tmp_node = next_node->prev;

	next_node->prev = node;

	if(tmp_node == (struct list_head*)(&head->first))
	{
		/*�˶��е�ǰֻ��һ���ڵ㣬�����뵽ͷ�ڵ㴦*/
		head->first = node;
	}
	else
	{
		tmp_node->next = node;
	}

	head->count++;
}

static inline struct list_head* squeue_deq(struct s_queue_head *head)
{
	struct list_head *ret = NULL;

	/*��ͷ��ȡ�ڵ�*/
	ret = head->first;
	if (NULL == ret)
	{
		return NULL;
	}

	if(NULL == (head->first = head->first->next))
	{
		/*���ȡ���ˣ����¸�ֵlast*/
		head->last_addr = &head->first;
	}
	else
	{
		/*����һ���ڵ��prevָ���Լ���prev*/
		ret->next->prev = ret->prev;
	}

	head->count--;
	return ret;
}

static inline struct list_head* squeue_deq_condition(struct s_queue_head *head,
							bool (*check)(struct list_head*, void* param1, void* param2, void* param3),
							void* param1, void* param2, void* param3)
{
	struct list_head *ret = NULL;

	/*��ͷ��ȡ�ڵ�*/
	ret = head->first;
	if (NULL == ret)
	{
		return NULL;
	}

	if (check(ret, param1, param2, param3) == false)
	{
		return NULL;
	}

	if(NULL == (head->first = head->first->next))
	{
		/*���ȡ���ˣ����¸�ֵlast*/
		head->last_addr = &head->first;
	}
	else
	{
		/*����һ���ڵ��prevָ���Լ���prev*/
		ret->next->prev = ret->prev;
	}

	head->count--;
	return ret;
}

static inline struct list_head* squeue_get_first(struct s_queue_head *head)
{
	return head->first;
}

static inline struct list_head* squeue_get_tail(struct s_queue_head *head)
{
	/*����β��Ϊ����β*/
	if (NULL == head->first)
	{
		return NULL;
	}

	return (struct list_head*)head->last_addr;
}

static inline struct list_head* squeue_get_prev(struct s_queue_head *head,
		struct list_head *node)
{
	if((node->prev) == (struct list_head*)(&head->first))
	{
		return NULL;
	}
	return (node->prev);
}

static inline bool squeue_is_empty(struct s_queue_head *head)
{
	if (NULL == head->first)
	{
		return true;
	}
	return false;
}

static inline void squeue_walk(struct s_queue_head *head,
				void (*walk_func)(struct list_head *node, void*, void*),
				void *param1, void *param2)
{
	struct list_head *node  = NULL;

	node = head->first;
	while(NULL != node)
	{
		walk_func(node, param1, param2);

		node = node->next;
	}
}

#define squeue_for_each_node(head, node) \
	for((node) = (head)->first; (node) != NULL; (node) = (node)->next)


#if 0

#include "nfs_atomic.h"

/*���ߺ�д�߶�ֻ��һ�����ȶ����д������Ҫ�򵥺ܶ࣬����ϸ��ʵ�ּ�
 * http://www.cnblogs.com/catch/p/3164829.html*/
typedef struct lock_free_q
{
	sq_node_t *head;
	sq_node_t *tail;

	nfs_atomic_t count;
	unsigned long int max_count; //��¼������
}lock_free_q_t;

#define LOCK_FREE_Q_CNT(queue)  nfs_atomic_read(&(queue)->count)
#define LOCK_FREE_Q_IS_EMPTY(queue)  (0 == nfs_atomic_read(&(queue)->count))

#define lf_queue_for_each(pos, queue) \
    for (pos = (queue)->head->next; pos != NULL; pos = pos->next)


static inline int lock_free_queue_init(lock_free_q_t *queue)
{
	sq_node_t *dummy = NULL;

	dummy = malloc(sizeof(sq_node_t));
	if (NULL == dummy)
	{
		fprintf(stderr, "fail to malloc dummy node.");
		return ERROR;
	}
	dummy->next = NULL;
	dummy->prev = NULL;

	queue->head = dummy;
	queue->tail = dummy;
	nfs_atomic_set(&queue->count, 0);
	queue->max_count = 10000000; ///�൱�ڲ����ƴ�С
	return OKF;
}

static inline void lock_free_queue_free(lock_free_q_t *queue)
{
	if ((queue->head == NULL) || (queue->tail == NULL))
	{
		fprintf(stderr, "head or tail is NULL.");
		assert(0);
		return;
	}

	int count = nfs_atomic_read(&queue->count);
	if ((queue->head != queue->tail)
			|| (0 != count))
	{
		fprintf(stderr, "queue is not empty when free, count %d.", count);
//		assert(0);
		return;
	}

	free(queue->head);

	queue->head = NULL;
	queue->tail = NULL;
}

static inline int lock_free_force_enqueue(sq_node_t *new_node, lock_free_q_t *queue)
{
	//���ܵ�ǰ���ٸ��ڵ㣬������

	/*queue->tailʼ�ն������,�Ҳ�ΪNULL*/
	new_node->next = NULL;
	queue->tail->next = new_node;
	queue->tail = new_node;

	nfs_atomic_inc(&queue->count);
	return OK;
}

static inline int lock_free_enqueue(sq_node_t *new_node, lock_free_q_t *queue)
{
	int cur_cnt = nfs_atomic_read(&queue->count);
	if (cur_cnt >= queue->max_count)
	{
		return ERROR;
	}

	/*queue->tailʼ�ն������,�Ҳ�ΪNULL*/
	new_node->next = NULL;
	queue->tail->next = new_node;
	queue->tail = new_node;

	nfs_atomic_inc(&queue->count);
	return OK;
}

static inline sq_node_t* lock_free_dequeue(lock_free_q_t *queue)
{
	sq_node_t *ret = NULL, *old_head = NULL;

	/*queue->headʼ�ն������,�Ҳ�ΪNULL*/
	if (NULL == queue->head->next)
	{
		return NULL;
	}

	old_head = queue->head;
	ret = queue->head->next;
	queue->head = ret;

	free(old_head);

	nfs_atomic_dec(&queue->count);
	return ret;
}
#endif


#ifdef __cplusplus
}
#endif

#endif
