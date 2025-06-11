#include <pthread.h>
#include <stdlib.h>
#include <genmc.h>

#include "my_queue.h"

#ifdef MAKE_ACCESSES_SC
# define relaxed memory_order_seq_cst
# define release memory_order_seq_cst
# define acquire memory_order_seq_cst
#else
# define relaxed memory_order_relaxed
# define release memory_order_release
# define acquire memory_order_acquire
#endif

typedef struct node {
	unsigned int value;
	_Atomic(struct node *) next;
} node_t;

typedef struct queue {
	_Atomic(node_t *) head;
	_Atomic(size_t) size;
} queue_t;

__VERIFIER_hp_t *hp_head;
__VERIFIER_hp_t *hp_next;

static node_t *new_node()
{
	return malloc(sizeof(node_t));
}

static void reclaim(_Atomic(node_t *) *node)
{
	void *_p_ = __atomic_load_n((void **) node, __ATOMIC_ACQUIRE);
	__VERIFIER_hp_retire(node);
}

static void unprotect0(_Atomic(node_t *) *node)
{
	void *_p_ = __atomic_load_n((void **) node, __ATOMIC_ACQUIRE);
	__VERIFIER_hp_clear(hp_head)	
}

static void unprotect1(_Atomic(node_t *) *node)
{
	void *_p_ = __atomic_load_n((void **) node, __ATOMIC_ACQUIRE);
	__VERIFIER_hp_clear(hp_next)	
}

void init_queue(queue_t *q, int num_threads)
{
	node_t* dummy = (node_t*)malloc(sizeof(node_t));
	atomic_init(&dummy->next, NULL);
    atomic_init(&q->head, dummy);
    atomic_init(&q->size, 0);

    // Initialize global hazard pointers
    hp_head = __VERIFIER_hp_alloc();
    hp_next = __VERIFIER_hp_alloc();
}

// static node_t *protect(__VERIFIER_hp_t *hp, queue_t *q, node_t *tail)
// {
// 	return __VERIFIER_hp_protect(hp, &q->tail);
// }

static node_t *protect0(_Atomic(node_t *) *ptr) {
    return __VERIFIER_hp_protect(hp_head, ptr);
}

static node_t *protect1(_Atomic(node_t *) *ptr) {
    return __VERIFIER_hp_protect(hp_next, ptr);
}

void enqueue(queue_t *q, unsigned int val)
{
	node_t* new_node = (node_t*)malloc(sizeof(node_t));
    new_node->value = val;
    node_t* prev;
    node_t* cur;

    // Hazard pointer protection
    do {
        prev = atomic_load_explicit(&q->head, acquire);
        cur = protect0(&prev->next);

        while (cur != NULL && cur->value < val) {  // Adjust comparison as needed
            prev = cur;
            cur = protect1(&cur->next);
        }

        atomic_store_explicit(&new_node->next, cur, release);
    } while (!atomic_compare_exchange_weak_explicit(&prev->next, &cur, new_node, release, relaxed));

    atomic_fetch_add_explicit(&q->size, 1, relaxed);
}

bool dequeue(queue_t *q, unsigned int *retVal)
{
    node_t* prev;
    node_t* cur;
    node_t* next;

    do {
        prev = atomic_load_explicit(&q->head, acquire);
        cur = protect0(&prev->next);

        // Traverse the list to find the node to delete
        while (cur != NULL && cur->value != *retVal) {  // Adjust comparison as needed
            prev = cur;
            cur = protect1(&cur->next);
        }

        if (cur == NULL) return false;  // Data not found

        // Physically delete the node by updating pointers
        next = atomic_load_explicit(&cur->next, acquire);
    } while (!atomic_compare_exchange_weak_explicit(&cur->next, &next, get_marked_reference(next), release, relaxed));

    // Physical removal of the logically deleted node
    if (atomic_compare_exchange_strong_explicit(&prev->next, &cur, next, release, relaxed)) {
        atomic_fetch_sub_explicit(&q->size, 1, relaxed);
        reclaim(&cur->next);
    } else {
        // Retry if necessary
        unprotect0(&prev->next);
        unprotect1(&cur->next);
    }

    return true;
}
