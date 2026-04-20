/*
 * SSN Mutex Implementation
 */

#include "ssn_mutex.h"
#include <stdlib.h>

ssn_mutex_t* ssn_mutex_create(void)
{
    ssn_mutex_t* mutex = (ssn_mutex_t*)malloc(sizeof(ssn_mutex_t));
    if (!mutex) {
        return NULL;
    }

    if (pthread_mutex_init(&mutex->mutex, NULL) != 0) {
        free(mutex);
        return NULL;
    }

    mutex->initialized = true;
    return mutex;
}

void ssn_mutex_destroy(ssn_mutex_t* mutex)
{
    if (!mutex) {
        return;
    }

    if (mutex->initialized) {
        pthread_mutex_destroy(&mutex->mutex);
        mutex->initialized = false;
    }

    free(mutex);
}

void ssn_mutex_lock(ssn_mutex_t* mutex)
{
    if (!mutex || !mutex->initialized) {
        return;
    }

    pthread_mutex_lock(&mutex->mutex);
}

void ssn_mutex_unlock(ssn_mutex_t* mutex)
{
    if (!mutex || !mutex->initialized) {
        return;
    }

    pthread_mutex_unlock(&mutex->mutex);
}

bool ssn_mutex_try_lock(ssn_mutex_t* mutex)
{
    if (!mutex || !mutex->initialized) {
        return false;
    }

    return (pthread_mutex_trylock(&mutex->mutex) == 0);
}

/*
void ssn_spinlock_init(ssn_spinlock_t* spinlock)
{
    if (!spinlock) {
        return;
    }

    if (pthread_spin_init(&spinlock->spinlock, PTHREAD_PROCESS_PRIVATE) == 0) {
        spinlock->initialized = true;
    } else {
        spinlock->initialized = false;
    }
}

void ssn_spinlock_destroy(ssn_spinlock_t* spinlock)
{
    if (!spinlock || !spinlock->initialized) {
        return;
    }

    pthread_spin_destroy(&spinlock->spinlock);
    spinlock->initialized = false;
}

void ssn_spinlock_lock(ssn_spinlock_t* spinlock)
{
    if (!spinlock || !spinlock->initialized) {
        return;
    }

    pthread_spin_lock(&spinlock->spinlock);
}

void ssn_spinlock_unlock(ssn_spinlock_t* spinlock)
{
    if (!spinlock || !spinlock->initialized) {
        return;
    }

    pthread_spin_unlock(&spinlock->spinlock);
}
*/

