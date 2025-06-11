#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include "ticketlock.c" // declares ticketlock_init, ticketlock_acquire, ticketlock_release

int main() {
    ticketlock_t lock;
    int shared_value = 0;

    // 1. Initialize the lock
    ticketlock_init(&lock);

    // 2. Acquire the lock and modify a shared value
    ticketlock_acquire(&lock);
    shared_value = 100;
    ticketlock_release(&lock);
    assert(shared_value == 100 && "shared value should be updated while locked");

    // 3. Re-acquire and update the value again
    ticketlock_acquire(&lock);
    shared_value += 23;
    ticketlock_release(&lock);
    assert(shared_value == 123 && "value should reflect second update");

    // 4. Final test to read while holding the lock
    ticketlock_acquire(&lock);
    int tmp = shared_value;
    ticketlock_release(&lock);
    assert(tmp == 123 && "final read should reflect correct value");

    printf("All single-threaded ticket lock tests passed!\n");
    return 0;
}
