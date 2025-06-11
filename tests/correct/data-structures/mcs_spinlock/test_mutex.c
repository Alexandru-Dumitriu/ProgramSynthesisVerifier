#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <stdatomic.h>
#include "fake.h"
#include "mcs_spinlock.h"

int main() {
    _Atomic(struct mcs_spinlock *) lock = NULL;
    struct mcs_spinlock node;
    int shared_value = 0;

    // 1. Lock and modify shared value
    mcs_spin_lock(&lock, &node);
    shared_value = 10;
    mcs_spin_unlock(&lock, &node);
    assert(shared_value == 10 && "shared value should be updated while locked");

    // 2. Lock again to test multiple lock/unlock cycles
    mcs_spin_lock(&lock, &node);
    shared_value += 5;
    mcs_spin_unlock(&lock, &node);
    assert(shared_value == 15 && "lock should work across multiple lock/unlock cycles");

    // 3. Final read after locking
    mcs_spin_lock(&lock, &node);
    int tmp = shared_value;
    mcs_spin_unlock(&lock, &node);
    assert(tmp == 15 && "final read should reflect correct value");

    printf("All single-threaded MCS lock tests passed!\n");
    return 0;
}
