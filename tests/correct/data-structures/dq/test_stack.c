#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include "dq copy.c"  // Ensure it declares and defines deque_try_push, deque_try_pop, etc.

int main() {
    // 1. Initialize the deque
    struct deque deq = {
        .bottom = ATOMIC_VAR_INIT(0),
        .top = ATOMIC_VAR_INIT(0)
    };
    for (int i = 0; i < LEN; i++) {
        deq.buffer[i] = 0;
    }

    int64_t val;
    int64_t res;

    // 2. Pop from empty deque
    res = deque_try_pop(&deq, &val);
    assert(res == -1 && "Pop should return -1 on empty deque");

    // 3. Steal from empty deque
    res = deque_try_steal(&deq, &val);
    assert(res == -1 && "Steal should return -1 on empty deque");

    // 4. Push a single element
    res = deque_try_push(&deq, 42);
    assert(res == 0 && "Push should succeed");

    // 5. Pop it
    res = deque_try_pop(&deq, &val);
    assert(res == 0 && val == 42 && "Popped value should match pushed value");

    // 6. Push multiple elements
    for (int64_t i = 1; i <= 2; i++) {
        res = deque_try_push(&deq, i);
        assert(res == 0 && "Push should succeed");
    }

    // 7. Steal them in FIFO order
    res = deque_try_steal(&deq, &val);
    assert(res == 0 && val == 1 && "Steal should return first pushed value");

    res = deque_try_steal(&deq, &val);
    assert(res == 0 && val == 2 && "Steal should return second pushed value");

    // 8. Deque should now be empty again
    res = deque_try_pop(&deq, &val);
    assert(res == -1 && "Pop should return -1 after all elements removed");

    printf("All deque tests passed!\n");
    return 0;
}
