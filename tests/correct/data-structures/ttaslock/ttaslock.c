#include "ttaslock.h"

static inline void ttaslock_init(struct ttaslock_s *l)
{
	atomic_init(&l->state, 0);
}

static inline void await_for_lock(struct ttaslock_s *l)
{
	while (atomic_load_explicit(&l->state, memory_order_relaxed) != 0)
		;
	return;
}

???

static inline void ttaslock_acquire(struct ttaslock_s *l)
{
	while (1) {
		await_for_lock(l);
		if (!try_acquire(l))
			return;
	}
}

static inline void ttaslock_release(struct ttaslock_s *l)
{
	atomic_store_explicit(&l->state, 0, memory_order_release);
}
