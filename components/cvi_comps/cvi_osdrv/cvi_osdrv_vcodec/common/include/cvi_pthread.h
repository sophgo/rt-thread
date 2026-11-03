#ifndef __CVI_PTHREAD_H__
#define __CVI_PTHREAD_H__

#include <base_ctx.h>
#include <pthread.h>
#include <semaphore.h>

#define MUTEX_T              pthread_mutex_t
#define MUTEX_INIT(mutex)    pthread_mutex_init(&mutex, NULL)
#define MUTEX_LOCK(mutex)    pthread_mutex_lock(&mutex)
#define MUTEX_UNLOCK(mutex)  pthread_mutex_unlock(&mutex)
#define MUTEX_DESTROY(mutex) pthread_mutex_destroy(&mutex)

#define SEM_T                   rt_sem_t
#define SEM_INIT(sem_name)      rt_sem_create(sem_name, 0, RT_IPC_FLAG_PRIO)
#define SEM_WAIT(sem)           rt_sem_take(sem, RT_WAITING_FOREVER)
#define SEM_TIMEWAIT(sem, ts)   rt_sem_take(sem, rt_tick_from_millisecond(ts.tv_sec * 1000 + ts.tv_nsec / 1000000))
#define SEM_POST(sem)           rt_sem_release(sem)
#define SEM_DESTROY(sem)        rt_sem_delete(sem)

#define PTHREAD_T pthread_t

#endif //__CVI_PTHREAD_H__
