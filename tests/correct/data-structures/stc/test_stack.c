#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include "stc.c" // Contains stack_init, stack_try_push, stack_try_pop, etc.

/***********************************************************
 * Minimal memory allocator for testing
 ***********************************************************/

#define MAX_NODES 10
struct stack_node pool[MAX_NODES];
int pool_index = 0;

int get_thread_num() { return 0; }

struct stack_node *new_node() {
	if (pool_index >= MAX_NODES) return NULL;
	return &pool[pool_index++];
}

int main() {
    struct stack s;
    stack_init(&s);
    int result, val;

    // 1. Try popping from an empty stack
    result = stack_try_pop(&s, &val);
    assert(result == -1 && "Pop should return -1 on empty stack");

    // 2. Push a single element
    result = stack_try_push(&s, 42);
    assert(result == 0 && "Push should succeed");

    // 3. Pop it back
    result = stack_try_pop(&s, &val);
    assert(result == 0 && val == 42 && "Pop should return the pushed value");

    // 4. Push multiple elements
    stack_try_push(&s, 1);
    stack_try_push(&s, 2);
    stack_try_push(&s, 3);

    // 5. Pop them in LIFO order
    result = stack_try_pop(&s, &val);
    assert(result == 0 && val == 3 && "Should pop last pushed value (3)");

    result = stack_try_pop(&s, &val);
    assert(result == 0 && val == 2 && "Should pop next value (2)");

    result = stack_try_pop(&s, &val);
    assert(result == 0 && val == 1 && "Should pop first pushed value (1)");

    // 6. Stack should now be empty again
    result = stack_try_pop(&s, &val);
    assert(result == -1 && "Pop should return -1 on empty stack");

    printf("All Treiber stack tests passed!\n");
    return 0;
}
