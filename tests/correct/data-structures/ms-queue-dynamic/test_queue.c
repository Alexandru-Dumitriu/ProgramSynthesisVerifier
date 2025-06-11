#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include "main.c"   // declares init_queue, enqueue, dequeue

int main() {
    queue_t q;
    unsigned int val;
    bool ok;

    // 1. Initialize with a single thread
    init_queue(&q, /* num_threads = */ 1);

    // 2. Test dequeue from an empty queue
    ok = dequeue(&q, &val);
    assert(ok == false && "dequeue should return false on empty queue");

    // 3. Enqueue then dequeue a single element
    enqueue(&q, 42);
    ok = dequeue(&q, &val);
    assert(ok == true && val == 42 && "dequeued value should match enqueued value");

    // 4. Enqueue multiple elements
    for (unsigned int i = 1; i <= 2; i++) {
        enqueue(&q, i);
    }
    // Dequeue them in FIFO order
    for (unsigned int i = 1; i <= 2; i++) {
        ok = dequeue(&q, &val);
        assert(ok == true && val == i && "dequeue should return values in enqueue order");
    }

    // 5. After draining, dequeue should again return false
    ok = dequeue(&q, &val);
    assert(ok == false && "dequeue should return false when queue is empty again");

    // 6. (Optional) Simple concurrency smoke test
    // // In a real test you'd spawn multiple threads calling enqueue/dequeue
    // printf("Concurrency tests omitted in this single-threaded test.\n");

    printf("All queue tests passed!\n");
    return 0;
}
