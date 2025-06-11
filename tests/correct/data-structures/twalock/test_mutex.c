#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include "main.c" // declares twalock_init, twalock_acquire, twalock_release

int main() {
    struct twalock_s lock;
    int shared_value = 0;

    // 1. Initialize the lock
    twalock_init(&lock);

    // 2. Acquire the lock and modify a shared value
    twalock_acquire(&lock);
    shared_value = 10;
    twalock_release(&lock);
    assert(shared_value == 10 && "shared value should be updated while locked");

    // 3. Re-acquire and update the value again
    twalock_acquire(&lock);
    shared_value += 5;
    twalock_release(&lock);
    assert(shared_value == 15 && "value should reflect second update");

    // 4. Final test to read while holding the lock
    twalock_acquire(&lock);
    int tmp = shared_value;
    twalock_release(&lock);
    assert(tmp == 15 && "final read should reflect correct value");

    printf("All single-threaded TWA lock tests passed!\n");
    return 0;
}
