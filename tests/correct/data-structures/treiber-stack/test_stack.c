#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include "main.c"  // Ensure it declares init_stack, push, pop, etc.

int main() {
    // 1. Initialize
    mystack_t stack;
    init_stack(&stack, /* num_threads = */ 1);

    // 2. Test popping from an empty stack
    unsigned int result = pop(&stack);
    assert(result == 0 && "Pop should return 0 on empty stack");

    // 3. Push a single element
    push(&stack, 42);
    result = pop(&stack);
    assert(result == 42 && "Popped value should match pushed value");

    // 4. Push multiple elements
    for (unsigned int i = 1; i <= 2; i++) {
        push(&stack, i);
    }
    // Pop them in LIFO order
    for (unsigned int i = 2; i >= 1; i--) {
        result = pop(&stack);
        assert(result == i && "Pop should return values in reverse order");
    }

    // 5. Edge cases or concurrency (simplified single-thread)
    // In a real concurrency test, you'd spawn threads and call push/pop concurrently.

    printf("All tests passed!\n");
    return 0;
}
