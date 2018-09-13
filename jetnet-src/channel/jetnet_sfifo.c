#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "jetnet_sfifo.h"
#include "jetnet_malloc.h"

jetnet_sfifo_t* jetnet_shm_kfifo_create(key_t key, size_t size){
	unsigned int real_size = buffer_size_2_kfifo_size((unsigned int)size);
	real_size += 4;
	int	shm_id = jetnet_shm_create(key,real_size);
	if(shm_id == -1){
		printf("share memory create error!errno=%d , errmsg=%s \n", errno, strerror(errno));
		return NULL;
	}
	void*adress = jetnet_shm_mnt(shm_id,false);
	if((void *)-1 == adress){
		jetnet_shm_destory(shm_id);
		return NULL;
	}
	void*fifo_addr = (void*)((char*)adress + 4);
	unsigned int fifo_size = real_size - 4;
	jetnet_kfifo_t*fifo = kfifo_create((unsigned char *)fifo_addr, fifo_size);
	if(!fifo){
		jetnet_shm_shutdown(adress);
		jetnet_shm_destory(shm_id);
		return NULL;
	}
	//write the len to the first 4 byte buffer
	*((uint32_t*)adress) = (uint32_t)real_size;
	
	jetnet_sfifo_t*ret = (jetnet_sfifo_t*)jetnet_malloc(sizeof(jetnet_sfifo_t));
	ret->gen_flag = 1;
	ret->fifo = fifo;
	ret->shm_id = shm_id;
	ret->real_addr = adress;
	ret->key = key;
	return ret;
}

jetnet_sfifo_t* jetnet_shm_kfifo_open(key_t key){
	int	shm_id = jetnet_shm_open(key);
	if(shm_id == -1)
		return NULL;
	void*adress = jetnet_shm_mnt(shm_id,false);
	if((void *)-1 == adress){
		return NULL;
	}
	uint32_t real_size = *(uint32_t*)adress;	
	void*fifo_addr = (void*)((char*)adress + 4);
	unsigned int fifo_size = real_size - 4;
	jetnet_kfifo_t*fifo = kfifo_open((unsigned char *)fifo_addr, fifo_size);
	if(!fifo){
		jetnet_shm_shutdown(adress);
		return NULL;
	}
	jetnet_sfifo_t*ret = (jetnet_sfifo_t*)jetnet_malloc(sizeof(jetnet_sfifo_t));
	ret->gen_flag = 0;
	ret->fifo = fifo;
	ret->shm_id = shm_id;
	ret->real_addr = adress;
	ret->key = key;
	return ret;
}

void	jetnet_shm_kfifo_close(jetnet_sfifo_t*sff){
	jetnet_shm_shutdown(sff->real_addr);
	if(sff->gen_flag)
		jetnet_shm_destory(sff->shm_id);
	jetnet_free(sff);
}
