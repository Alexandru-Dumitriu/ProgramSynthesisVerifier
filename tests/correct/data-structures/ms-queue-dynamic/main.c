#include "my_queue.c"

#ifndef MAX_THREADS
# define MAX_THREADS 32
#endif

#ifdef CONFIG_QUEUE_READERS
#define DEFAULT_READERS (CONFIG_QUEUE_READERS)
#else
#define DEFAULT_READERS 2
#endif

#ifdef CONFIG_QUEUE_WRITERS
#define DEFAULT_WRITERS (CONFIG_QUEUE_WRITERS)
#else
#define DEFAULT_WRITERS 3
#endif

#ifdef CONFIG_QUEUE_RDWR
#define DEFAULT_RDWR (CONFIG_QUEUE_RDWR)
#else
#define DEFAULT_RDWR 1
#endif

int readers = DEFAULT_READERS, writers = DEFAULT_WRITERS, rdwr = DEFAULT_RDWR;

static queue_t *queue;
static int num_threads;

queue_t myqueue;
int param[MAX_THREADS];
unsigned int input[MAX_THREADS];
unsigned int output[MAX_THREADS];
pthread_t threads[MAX_THREADS];

int __thread tid;

void set_thread_num(int i)
{
	tid = i;
}

int get_thread_num()
{
	return tid;
}

bool succ[MAX_THREADS];

void *threadW(void *param)
{
	unsigned int val;
	int pid = *((int *)param);

	set_thread_num(pid);

	input[pid] = pid * 10 + 1;
//	input[pid+1] = pid * 10 + 2;
	enqueue(queue, input[pid]);
//	printf("put in0: %d\n", input[pid]);
//	enqueue(queue, input[pid+1]);
//	printf("put in1: %d\n", input[pid+1]);
//	succ[pid] = dequeue(queue, &output[pid]);
//	printf("T: Enqueue: %d\n", input[pid]);
	return NULL;
}

void *threadR(void *param)
{
	unsigned int val;
	int pid = *((int *)param);

	set_thread_num(pid);

//	input[pid] = pid * 10;
//	enqueue(queue, input[pid]);
	succ[pid] = dequeue(queue, &output[pid]);
//	printf("got from0: %d\n", output[pid]);
//	succ[pid+1] = dequeue(queue, &output[pid+1]);
//	printf("got from1: %d\n", output[pid+1]);
	return NULL;
}

void *threadRW(void *param)
{
	unsigned int val;
	int pid = *((int *)param);

	set_thread_num(pid);

	input[pid] = pid * 10;
	enqueue(queue, input[pid]);
//	printf("pid: %d\n", pid);
	succ[pid] = dequeue(queue, &output[pid]);
//	printf("got out: %d\n", output[pid]);
	return NULL;
}
