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
	_Atomic(node_t *) tail;
} queue_t;

// __VERIFIER_hp_t *hp_head;
// __VERIFIER_hp_t *hp_next;

static node_t *new_node()
{
	return malloc(sizeof(node_t));
}

// static void reclaim(_Atomic(node_t *) *node)
// {
// 	node_t *p = atomic_load_explicit(node, acquire);
//     __VERIFIER_hp_retire(p);
// }
static void reclaim(_Atomic(node_t *) *ptr, node_t *node)
{
	node_t *p = atomic_load_explicit(ptr, acquire);
	__VERIFIER_hp_retire(node);
}

// static void unprotect0(_Atomic(node_t *) *node)
// {
// 	void *_p_ = __atomic_load_n((void **) node, __ATOMIC_ACQUIRE);
// 	__VERIFIER_hp_clear(hp_head)	
// }

static void unprotect0(_Atomic(node_t *) *ptr, __VERIFIER_hp_t *hp)
{
	node_t *p = atomic_load_explicit(ptr, acquire);
	__VERIFIER_hp_clear(hp);	
}

static void unprotect1(_Atomic(node_t *) *ptr, __VERIFIER_hp_t *hp)
{
	node_t *p = atomic_load_explicit(ptr, acquire);
	__VERIFIER_hp_clear(hp);	
}

// static void unprotect1(_Atomic(node_t *) *node)
// {
// 	void *_p_ = __atomic_load_n((void **) node, __ATOMIC_ACQUIRE);
// 	__VERIFIER_hp_clear(hp_next)	
// }

void init_queue(queue_t *q, int num_threads)
{
	node_t *dummy = new_node();
	atomic_init(&dummy->next, NULL);
	atomic_init(&q->head, dummy);
	atomic_init(&q->tail, dummy);
	// hp_head = __VERIFIER_hp_alloc();
	// hp_next = __VERIFIER_hp_alloc();
}

// static node_t *protect0(_Atomic(node_t *) *ptr) {
//     return __VERIFIER_hp_protect(hp_head, ptr);
// }

static node_t *protect0(_Atomic(node_t *) *ptr, __VERIFIER_hp_t *hp) {
    return __VERIFIER_hp_protect(hp, ptr);
}

static node_t *protect1(_Atomic(node_t *) *ptr, __VERIFIER_hp_t *hp) {
    return __VERIFIER_hp_protect(hp, ptr);
}

// static node_t *protect1(_Atomic(node_t *) *ptr) {
//     return __VERIFIER_hp_protect(hp_next, ptr);
// }

void enqueue(queue_t *q, unsigned int val)
{
	node_t *tail, *next;

	node_t *node = new_node();
	node->value = val;
	node->next = NULL;

	__VERIFIER_hp_t *hp = __VERIFIER_hp_alloc();
	while (true) {
		tail = protect0(&q->tail, hp);
		next = atomic_load_explicit(&tail->next, acquire);
		if (tail != atomic_load_explicit(&q->tail, acquire))
			continue;

		if (next == NULL) {
			if (__VERIFIER_final_CAS(
				    atomic_compare_exchange_strong_explicit(&tail->next, &next,
									    node, release, release)))
				break;
		} else {
			__VERIFIER_helping_CAS(
				atomic_compare_exchange_strong_explicit(&q->tail, &tail, next,
									release, release);
			);
		}
	}
	unprotect0(&q->tail, hp);
	__VERIFIER_helped_CAS(
		atomic_compare_exchange_strong_explicit(&q->tail, &tail, node, release, release);
	);
}

bool dequeue(queue_t *q, unsigned int *retVal)
{
	node_t *head, *tail, *next, *prev;
	unsigned ret;

	__VERIFIER_hp_t *hp_head = __VERIFIER_hp_alloc();
	__VERIFIER_hp_t *hp_next = __VERIFIER_hp_alloc();
	while (true) {
		head = protect0(&q->head, hp_head);
		// prev = atomic_load_explicit(&q->head, acquire);
		tail = atomic_load_explicit(&q->tail, acquire);
		next = protect1(&head->next, hp_next);
		if (atomic_load_explicit(&q->head, acquire) != head)
			continue;
		if (head == tail) {
			if (next == NULL)
				return false;
			__VERIFIER_helping_CAS(
				atomic_compare_exchange_strong_explicit(&q->tail, &tail, next,
									release, release);
			);
		} else {
			ret = next->value;
			if (atomic_compare_exchange_strong_explicit(&q->head, &head, next,
								    release, release))
				break;
		}
	}
	*retVal = ret;
	unprotect0(&q->head, hp_head);
	unprotect1(&head->next, hp_next);
	// reclaim(&q->head);
	reclaim(&q->head, head);
	return true;
}
