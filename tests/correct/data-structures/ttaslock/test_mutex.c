#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include "ttaslock.c" // declares ttaslock_init, ttaslock_acquire, ttaslock_release

int main() {
    struct ttaslock_s lock;
    int shared_value = 0;

    // 1. Initialize the lock
    ttaslock_init(&lock);

    // 2. Acquire the lock and modify a shared value
    ttaslock_acquire(&lock);
    shared_value = 42;
    ttaslock_release(&lock);
    assert(shared_value == 42 && "shared value should be updated while locked");

    // 3. Re-acquire and test value update
    ttaslock_acquire(&lock);
    shared_value += 1;
    ttaslock_release(&lock);
    assert(shared_value == 43 && "value should reflect second lock-protected update");

    // 4. Final test to check value after locking and reading
    ttaslock_acquire(&lock);
    int tmp = shared_value;
    ttaslock_release(&lock);
    assert(tmp == 43 && "final read should reflect correct value");

    printf("All single-threaded TTAS lock tests passed!\n");
    return 0;
}
