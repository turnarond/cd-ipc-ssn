/*
 * SSN Mutex Interface
 */

#ifndef SSN_MUTEX_H
#define SSN_MUTEX_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

typedef struct ssn_mutex ssn_mutex_t;
typedef struct ssn_spinlock ssn_spinlock_t;

struct ssn_mutex {
    pthread_mutex_t mutex;
    bool initialized;
};

struct ssn_spinlock {
    pthread_spinlock_t spinlock;
    bool initialized;
};

ssn_mutex_t* ssn_mutex_create(void);
void ssn_mutex_destroy(ssn_mutex_t* mutex);
void ssn_mutex_lock(ssn_mutex_t* mutex);
void ssn_mutex_unlock(ssn_mutex_t* mutex);
bool ssn_mutex_try_lock(ssn_mutex_t* mutex);

void ssn_spinlock_init(ssn_spinlock_t* spinlock);
void ssn_spinlock_destroy(ssn_spinlock_t* spinlock);
void ssn_spinlock_lock(ssn_spinlock_t* spinlock);
void ssn_spinlock_unlock(ssn_spinlock_t* spinlock);

#endif

