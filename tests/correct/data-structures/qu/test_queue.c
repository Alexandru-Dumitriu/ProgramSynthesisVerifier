#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include "main.c"  // declares queue_init, queue_try_enq, queue_try_deq, etc.

// Minimal implementation for testing
int thread_id = 0;

#define MAX_NODES 10
struct queue_node pool[MAX_NODES];
int pool_index = 0;

int main() {
	struct queue q;
	int val;
	int ok;

	// 1. Initialize the queue
	queue_init(&q);
	assert(queue_is_initialized(&q) && "queue should be marked as initialized");

	// 2. Dequeue from an empty queue should return -1
	ok = queue_try_deq(&q, &val);
	assert(ok == -1 && "dequeue from empty queue should return -1");

	// 3. Enqueue and then dequeue a single item
	ok = queue_try_enq(&q, 42);
	assert(ok == 0 && "enqueue should succeed");

	ok = queue_try_deq(&q, &val);
	assert(ok == 0 && val == 42 && "dequeue should return the enqueued value");

	// 4. Enqueue multiple items and check FIFO order
	ok = queue_try_enq(&q, 1);
	assert(ok == 0);

	ok = queue_try_enq(&q, 2);
	assert(ok == 0);

	ok = queue_try_enq(&q, 3);
	assert(ok == 0);

	ok = queue_try_deq(&q, &val);
	assert(ok == 0 && val == 1 && "FIFO order expected: 1");

	ok = queue_try_deq(&q, &val);
	assert(ok == 0 && val == 2 && "FIFO order expected: 2");

	ok = queue_try_deq(&q, &val);
	assert(ok == 0 && val == 3 && "FIFO order expected: 3");

	// 5. Dequeue again should return -1 (empty)
	ok = queue_try_deq(&q, &val);
	assert(ok == -1 && "dequeue from empty queue should return -1");

	printf("All single-threaded MS queue tests passed!\n");
	return 0;
}
