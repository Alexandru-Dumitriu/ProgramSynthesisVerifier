#include <stdatomic.h>

struct queue;
typedef struct queue queue_t;

void init_queue(queue_t *q, int num_threads);
void enqueue(queue_t *q, unsigned int val);
bool dequeue(queue_t *q, unsigned int *retVal);

#ifndef __GENMC_H__
#define __GENMC_H__

#ifdef __cplusplus
extern "C"
{
#endif
/*
 * An opaque type for hazard pointers.
 * The interface is similar to the one for atomic variables
 * (i.e., all operations on hazard pointers take the address
 * of a hazard pointer object as a parameter, etc)
 */
typedef struct { void *__dummy; } __VERIFIER_hazptr_t;

/* Hazard pointer functions */
extern __VERIFIER_hazptr_t *__VERIFIER_hazptr_alloc(void)  __attribute__ ((__nothrow__));
extern void __VERIFIER_hazptr_protect(__VERIFIER_hazptr_t *hp, void *p) __attribute__ ((__nothrow__));
extern void __VERIFIER_hazptr_clear(__VERIFIER_hazptr_t *hp) __attribute__ ((__nothrow__));
extern void __VERIFIER_hazptr_free(__VERIFIER_hazptr_t *hp) __attribute__ ((__nothrow__));
extern void __VERIFIER_hazptr_retire(void *p) __attribute__ ((__nothrow__));
typedef __VERIFIER_hazptr_t __VERIFIER_hp_t;
extern void __VERIFIER_annotate_begin(int mask) __attribute__ ((__nothrow__));
extern void __VERIFIER_annotate_end(int mask) __attribute__ ((__nothrow__));
#define GENMC_KIND_HELPED  0x00020000
#define GENMC_KIND_HELPING 0x00040000
#define __VERIFIER_helped_CAS(c)			\
do {							\
	__VERIFIER_annotate_begin(GENMC_KIND_HELPED);	\
	c;						\
	__VERIFIER_annotate_end(GENMC_KIND_HELPED);	\
} while (0)
#define __VERIFIER_helping_CAS(c)			\
do {							\
	__VERIFIER_annotate_begin(GENMC_KIND_HELPING);	\
	c;						\
	__VERIFIER_annotate_end(GENMC_KIND_HELPING);	\
} while (0)

/*
 * Allocates a hazard pointer (and links it to the global list of hazard pointers).
 * Returns an (opaque) __VERIFIER_hp_t.
 */
#define __VERIFIER_hp_alloc()						\
	__VERIFIER_hazptr_alloc()					\

/*
 * Protects P using HP; returns the value protected.
 * (HP needs to be the address of a hazard pointer, while
 * p needs to be the address of an (atomic) variable.)
 */
#define __VERIFIER_hp_protect(hp, p)					\
({									\
	void *_p_ = __atomic_load_n((void **) p, __ATOMIC_ACQUIRE);	\
	__VERIFIER_hazptr_protect(hp, _p_);				\
	_p_;								\
})

/*
 * Clears HP's protection
 */
#define __VERIFIER_hp_clear(hp)			\
	__VERIFIER_hazptr_clear(hp);

/*
 * Deallocates HP (and removes it from the global list).
 * (HP needs to be the address of a hazard pointer.)
 */
#define __VERIFIER_hp_free(hp)			\
	__VERIFIER_hazptr_free(hp)

/*
 * Deallocates P after it ensures that it is no longer protected.
 * This is equivalent to adding P to a list of items that will
 * eventually be freed.
 */
#define __VERIFIER_hp_retire(p)			\
	__VERIFIER_hazptr_retire(p)

/*
 * The signature of a recovery routine to be specified
 * by the user. This routine will run after each execution,
 * if the checker is run with the respective flags enabled
 */
void __VERIFIER_recovery_routine(void) __attribute__ ((__nothrow__));

/*
 * All the file opeartions before this barrier will
 * have persisted to memory when the recovery routine runs.
 * Should be used only once.
 */
void __VERIFIER_pbarrier(void) __attribute__ ((__nothrow__));

#ifdef __cplusplus
}
#endif

#endif /* __GENMC_H__ */
