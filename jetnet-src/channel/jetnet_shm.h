#ifndef __JETNET_SHM_H_INCLUDED
#define __JETNET_SHM_H_INCLUDED

#include <sys/types.h>
#include <sys/shm.h>
#include <stdbool.h>
#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

/**
 *On success, a valid shared memory identifier is returned.  On error,
 *     -1 is returned, and errno is set to indicate the error.
 */
static inline int jetnet_shm_create(key_t key,size_t size){
	return shmget(key, size, IPC_CREAT|IPC_EXCL|0666);
}

/**
 *On success, a valid shared memory identifier is returned.  On error,
 *      -1 is returned, and errno is set to indicate the error.
 */
static inline  int jetnet_shm_open(key_t key){
	return shmget(key, 0, 0666);
}

/**
 *On success, returns the address of the attached shared memory
 *segment; on error, (void *) -1 is returned, and errno is set to
 *indicate the cause of the error.
 */
static inline void*	jetnet_shm_mnt(int shm_id, bool read_only){
	int shmflg = 0;
	if(read_only)
		shmflg |= SHM_RDONLY;
	return shmat(shm_id, NULL,read_only);
}

/**
 *On success, returns 0; on error -1 is returned, and errno is
 *set to indicate the cause of the error.
 */   
static inline int jetnet_shm_shutdown(const void *shmaddr){
	return shmdt(shmaddr);
}

/**
 *operations return 0 on success.
 *On error, -1 is returned, and errno is set appropriately.
 */
static inline int jetnet_shm_destory(int shm_id){
	return shmctl(shm_id, IPC_RMID, 0);
}

#if defined(__cplusplus)
}
#endif

#endif /* __JETNET_SHM_H_INCLUDED */