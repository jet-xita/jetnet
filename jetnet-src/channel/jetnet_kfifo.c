#include <stdbool.h>
#include <string.h>
#include "jetnet_kfifo.h"

#ifndef MIN
# define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
# define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

/**
 * padding struct to avoid false sharing.
 */
typedef struct jetnet_cache_line_padding_t{
	uint32_t p1,p2,p3,p4,p5,p6,p7,p8;
}jetnet_cache_line_padding;

/**
 * define the kfifo struct
 */
struct jetnet_kfifo_t{
    uint32_t size;         /* the size of the allocated buffer */
	jetnet_cache_line_padding size_padding;
    uint32_t in;           /* data is added at offset (in % size) */
	jetnet_cache_line_padding in_padding;
    uint32_t out;          /* data is extracted from off. (out % size) */
	jetnet_cache_line_padding out_padding;
	int lock;
};

#define	GET_FIFO_BUFFER(fifo) ((unsigned char*)(fifo) + (unsigned int)sizeof(jetnet_kfifo_t))

static inline  bool is_power_of_2(unsigned int n){
    return (n != 0 && ((n & (n - 1)) == 0));
}

static unsigned int roundup_pow_of_2(unsigned int s){
    if (s == 0)
        return 0;
    unsigned int position = 0;
	unsigned int i;
    for (i = s; i != 0; i >>= 1)
        position++;
    return (unsigned int)(1 << position);
}

unsigned int buffer_size_2_kfifo_size(unsigned int buffer_size){
	return (unsigned int)sizeof(jetnet_kfifo_t) + roundup_pow_of_2(buffer_size);
}

static inline void spinlock_init(jetnet_kfifo_t*fifo) {
	fifo->lock = 0;
}

static inline void spinlock_lock(jetnet_kfifo_t*fifo) {
	while (__sync_lock_test_and_set(&fifo->lock,1)) {}
}

static inline int spinlock_trylock(jetnet_kfifo_t*fifo) {
	return __sync_lock_test_and_set(&fifo->lock,1) == 0;
}

static inline void spinlock_unlock(jetnet_kfifo_t*fifo) {
	__sync_lock_release(&fifo->lock);
}

jetnet_kfifo_t *kfifo_create(unsigned char *buffer, unsigned int size){
	unsigned int st_size = (unsigned int)sizeof(jetnet_kfifo_t);
	unsigned int buffer_size = size - st_size;
	if(buffer_size <= 0 || !is_power_of_2(buffer_size))
		return NULL;
	jetnet_kfifo_t* fifo = (jetnet_kfifo_t*)buffer;
	fifo->size = buffer_size;
	fifo->in = fifo->out = 0;
	spinlock_init(fifo);
	return fifo;
}

jetnet_kfifo_t *kfifo_open(unsigned char *buffer, unsigned int size){
	unsigned int st_size = (unsigned int)sizeof(jetnet_kfifo_t);
	unsigned int buffer_size = size - st_size;
	if(buffer_size <= 0 || !is_power_of_2(buffer_size))
		return NULL;
	jetnet_kfifo_t* fifo = (jetnet_kfifo_t*)buffer;
	return fifo;
}

static inline void kfifo_internal_put(jetnet_kfifo_t *fifo, unsigned char*fifo_buffer, unsigned char *buffer, unsigned int len){
	unsigned int n = MIN(len, fifo->size - (fifo->in & (fifo->size - 1)));
	if(n > 0)
		memcpy(fifo_buffer + (fifo->in & (fifo->size - 1)), buffer, n);
	if(len > n)
		memcpy(fifo_buffer, buffer + n, len - n);
	fifo->in += len;
}

int kfifo_raw_put_iov(jetnet_kfifo_t *fifo,jetnet_fifo_iov_t*iov){
	int ret = -1;
	if(!iov || iov->size == 0)
		return ret;
	unsigned int total_len = 0;
	int i;
	for(i=0; i< (int)iov->size; i++)
		total_len += (unsigned int)iov->vec[i].size;
	unsigned char*fifo_buffer = GET_FIFO_BUFFER(fifo);
	spinlock_lock(fifo);
	if( fifo->size - fifo->in + fifo->out < total_len ){
		ret = -1;
	}else{
		for(i=0; i< (int)iov->size; i++)
			kfifo_internal_put(fifo,fifo_buffer,iov->vec[i].buf,(unsigned int)iov->vec[i].size);
		ret = 0;
	}
	spinlock_unlock(fifo);
    return ret;
}

// 0 succeed. -1 hasn't empty space for put.
int kfifo_raw_put(jetnet_kfifo_t *fifo,unsigned char *buffer, unsigned int len){
	jetnet_fifo_iov_t iov;
	iov.size = 1;
	iov.vec[0].buf = buffer;
	iov.vec[0].size = len;
	return kfifo_raw_put_iov(fifo,&iov);
}

// 0 succeed. -1 hasn't empty space for put.
int kfifo_pkg_put(jetnet_kfifo_t *fifo,unsigned char *buffer, unsigned int len){
	uint32_t s = (uint32_t)len;
	jetnet_fifo_iov_t iov;
	iov.size = 2;
	iov.vec[0].buf = &s;
	iov.vec[0].size = 4;
	iov.vec[1].buf = buffer;
	iov.vec[1].size = len;
	return kfifo_raw_put_iov(fifo,&iov);
}

int kfifo_pkg_put_iov(jetnet_kfifo_t *fifo,jetnet_fifo_iov_t*iov){
	if(!iov || iov->size == 0 || iov->size >= MAX_FIFO_IO)
		return -1;
	uint32_t total_len = 0;
	int i;
	for(i=0; i< (int)iov->size; i++)
		total_len += (uint32_t)iov->vec[i].size;
	
	jetnet_fifo_iov_t _iov;
	for(i = (int)iov->size; i > 0; i--)
		_iov.vec[i] = iov->vec[i-1];
	_iov.size = iov->size;
	_iov.vec[0].buf = &total_len;
	_iov.vec[0].size = 4;
	_iov.size++;
	return kfifo_raw_put_iov(fifo,&_iov);
}

// 0 succeed. -1 the data length is less then len.
int kfifo_raw_get(jetnet_kfifo_t *fifo,unsigned char *buffer, unsigned int len){
	int ret;
    unsigned int n;	
	unsigned char*fifo_buffer = GET_FIFO_BUFFER(fifo);
	spinlock_lock(fifo);
	if( fifo->in - fifo->out < len ){
		ret = -1;
	}else{
		n = MIN(len, fifo->size - (fifo->out & (fifo->size - 1)));
		if(n > 0)
			memcpy(buffer, fifo_buffer + (fifo->out & (fifo->size - 1)), n);
		if(len > n)
			memcpy(buffer + n, fifo_buffer, len - n);
		fifo->out += len;
		ret = 0;
	}
	spinlock_unlock(fifo);
    return ret;
}

// 0 succeed. -1 hasn't completed pkg for get. -2 the input buffer is too less to receive pkg.
int kfifo_pkg_get(jetnet_kfifo_t *fifo,unsigned char *buffer, unsigned int* len){
	int ret;
    unsigned int n;	
	unsigned char*fifo_buffer = GET_FIFO_BUFFER(fifo);
	uint32_t cl;
	unsigned char *clb = (unsigned char *)&cl;	
	spinlock_lock(fifo);
	if(fifo->in - fifo->out <= 4){
		ret = -1;
	}else{
		//get 4 bytes
		n = MIN(4, fifo->size - (fifo->out & (fifo->size - 1)));
		if(n > 0)
			memcpy(clb, fifo_buffer + (fifo->out & (fifo->size - 1)), n);
		if(4 > n)
			memcpy(clb + n, fifo_buffer, 4 - n);
		if(fifo->in - fifo->out < cl + 4){
			ret = -1;
		}else{
			if( *len < cl ){
				*len = cl;
				ret = -2;
			}else{
				fifo->out += 4;
				//get cl bytes
				n = MIN(cl, fifo->size - (fifo->out & (fifo->size - 1)));
				if(n > 0)
					memcpy(buffer, fifo_buffer + (fifo->out & (fifo->size - 1)), n);
				if(cl > n)
					memcpy(buffer + n, fifo_buffer, cl - n);
				fifo->out += cl;
				
				*len = cl;
				ret = 0;
			}			
		}
	}
	spinlock_unlock(fifo);
    return ret;
}