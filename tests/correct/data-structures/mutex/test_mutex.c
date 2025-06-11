#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include "main.c" // declares mutex_init, mutex_lock, mutex_unlock

int main() {
    mutex_t m;
    int shared_value = 0;

    // 1. Initialize the mutex
    mutex_init(&m);

    // 2. Lock and unlock should protect access in single thread
    mutex_lock(&m);
    shared_value = 123;
    mutex_unlock(&m);
    assert(shared_value == 123 && "mutex should allow updating shared value");

    // 3. Locking twice without unlocking (single-threaded) is not supported,
    //    so we avoid testing that — mutex is non-recursive.

    // 4. Lock again to test multiple lock-unlock sequences
    mutex_lock(&m);
    shared_value++;
    mutex_unlock(&m);
    assert(shared_value == 124 && "mutex should work across multiple lock/unlock cycles");

    // 5. Final test to ensure the shared value remains correct
    mutex_lock(&m);
    int tmp = shared_value;
    mutex_unlock(&m);
    assert(tmp == 124 && "final read should reflect correct value");

    printf("All single-threaded mutex tests passed!\n");
    return 0;
}
