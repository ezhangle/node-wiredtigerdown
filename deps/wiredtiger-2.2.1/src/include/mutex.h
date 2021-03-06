/*-
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

/*
 * Condition variables:
 *
 * WiredTiger uses standard pthread condition variables to signal between
 * threads, and for locking operations that are expected to block.
 */
struct __wt_condvar {
	const char *name;		/* Mutex name for debugging */

	pthread_mutex_t mtx;		/* Mutex */
	pthread_cond_t  cond;		/* Condition variable */

	int waiters;			/* Numbers of waiters, or
					   -1 if signalled with no waiters. */
};

/*
 * Read/write locks:
 *
 * WiredTiger uses standard pthread rwlocks to get shared and exclusive access
 * to resources.
 */
struct __wt_rwlock {
	const char *name;		/* Lock name for debugging */

	pthread_rwlock_t rwlock;	/* Read/write lock */
};

/*
 * Spin locks:
 *
 * These used for cases where fast mutual exclusion is needed (where operations
 * done while holding the spin lock are expected to complete in a small number
 * of instructions).
 */
#define	SPINLOCK_GCC			0
#define	SPINLOCK_PTHREAD_MUTEX		1
#define	SPINLOCK_PTHREAD_MUTEX_LOGGING	2

#if SPINLOCK_TYPE == SPINLOCK_GCC

typedef volatile int
    WT_SPINLOCK WT_GCC_ATTRIBUTE((aligned(WT_CACHE_LINE_ALIGNMENT)));

#elif SPINLOCK_TYPE == SPINLOCK_PTHREAD_MUTEX ||\
	SPINLOCK_TYPE == SPINLOCK_PTHREAD_MUTEX_LOGGING

typedef struct {
	pthread_mutex_t lock;

	uint64_t counter;		/* Statistics: counter */

	const char *name;		/* Statistics: mutex name */
	int8_t id;			/* Statistics: current holder ID */

	int8_t initialized;		/* Lock initialized, for cleanup */
} WT_SPINLOCK WT_GCC_ATTRIBUTE((aligned(WT_CACHE_LINE_ALIGNMENT)));

#else

#error Unknown spinlock type

#endif
