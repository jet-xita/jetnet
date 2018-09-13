#ifndef __JETNET_KFIFO_H_INCLUDED
#define __JETNET_KFIFO_H_INCLUDED
#include <stdint.h>
#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define MAX_FIFO_IO		8

typedef struct jetnet_fifo_io_t{
	void* buf;
	size_t size;
}jetnet_fifo_io_t;

typedef struct jetnet_fifo_iov_t{
	int size;
	jetnet_fifo_io_t vec[MAX_FIFO_IO];
}jetnet_fifo_iov_t;

struct jetnet_kfifo_t;
typedef struct jetnet_kfifo_t jetnet_kfifo_t;

/**
 * calculate the share memory size for kfifo
 */
extern uint32_t buffer_size_2_kfifo_size(uint32_t buffer_size);

/**
 * create an kfifo object on the input buffer
 */
extern jetnet_kfifo_t* kfifo_create(unsigned char *buffer, uint32_t size);

/**
 * open an kfifo object on the input buffer
 */
extern jetnet_kfifo_t* kfifo_open(unsigned char *buffer, uint32_t size);

/**
 * 0 succeed. -1 hasn't empty space for put.
 */
extern int kfifo_raw_put(jetnet_kfifo_t *fifo,unsigned char *buffer, uint32_t len);

/**
 * 0 succeed. -1 hasn't empty space for put.
 */
extern int kfifo_raw_put_iov(jetnet_kfifo_t *fifo,jetnet_fifo_iov_t*iov);


/**
 * 0 succeed. -1 hasn't empty space for put.
 */
extern int kfifo_pkg_put(jetnet_kfifo_t *fifo,unsigned char *buffer, uint32_t len);

/**
 * 0 succeed. -1 hasn't empty space for put.
 */
extern int kfifo_pkg_put_iov(jetnet_kfifo_t *fifo,jetnet_fifo_iov_t*iov);

/**
 * 0 succeed. -1 the data length is less then len.
 */
extern int kfifo_raw_get(jetnet_kfifo_t *fifo,unsigned char *buffer, uint32_t len);

/**
 * 0 succeed. -1 hasn't completed pkg for get. -2 the input buffer is too less to receive pkg.
 the len param return the msg len when the return value is 0 or -2.
 */
extern int kfifo_pkg_get(jetnet_kfifo_t *fifo,unsigned char *buffer, uint32_t* len);

#if defined(__cplusplus)
}
#endif

#endif /* __JETNET_KFIFO_H_INCLUDED */