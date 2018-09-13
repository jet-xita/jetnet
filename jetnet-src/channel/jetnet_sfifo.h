#ifndef __JETNET_SFIFO_H_INCLUDED
#define __JETNET_SFIFO_H_INCLUDED

#include "jetnet_shm.h"
#include "jetnet_kfifo.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct jetnet_sfifo_t{
	jetnet_kfifo_t *fifo;
	void* real_addr;
	int shm_id;
	int gen_flag;
	key_t key;
}jetnet_sfifo_t;

/**
 * create jetnet_sfifo_t object on share memory
 */
extern jetnet_sfifo_t* jetnet_shm_kfifo_create(key_t key, size_t size);

/**
 * open jetnet_sfifo_t object on share memory
 */
extern jetnet_sfifo_t* jetnet_shm_kfifo_open(key_t key);

/**
 * close the jetnet_sfifo_t object
 */
extern void	jetnet_shm_kfifo_close(jetnet_sfifo_t*sff);

#if defined(__cplusplus)
}
#endif

#endif /* __JETNET_SFIFO_H_INCLUDED */