#ifndef JETNET_IOBUF_H_INCLUDED
#define JETNET_IOBUF_H_INCLUDED

#include <stdint.h>
#include <sys/types.h>
#include "jetnet_malloc.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define JETNET_MAX_IOVEC 4

/**
 * read buffer struct for network module.
 */
typedef struct jetnet_rb_t{
	struct jetnet_rb_t* next;
	char * buffer;
	int size;
}jetnet_rb_t;

/**
 * read buffer list struct for network module.
 */
typedef struct jetnet_rbl_t {
	int offset;
	int size;
	jetnet_rb_t * head;
	jetnet_rb_t * tail;
}jetnet_rbl_t;

/**
 * io vector for network module.
 */
typedef struct jetnet_iov_t {
    void  *iov_base;
    size_t iov_len;
}jetnet_iov_t;

/**
 * write buffer struct for network module.
 */
typedef struct jetnet_wb_t{
	struct jetnet_wb_t * next;
	jetnet_iov_t vec[JETNET_MAX_IOVEC];
	int vec_count;
	int offset;
}jetnet_wb_t;

/**
 * write buffer list struct for network module.
 */
typedef struct jetnet_wbl_t {
	jetnet_wb_t * head;
	jetnet_wb_t * tail;
}jetnet_wbl_t;

/**
 * free an network read buffer object
 */
static inline void jetnet_rb_free(jetnet_rb_t *rb){
	jetnet_free((void*)rb->buffer);
	jetnet_free((void*)rb);
}

/**
 * free an network read buffer list object
 */
void jetnet_rbl_free(jetnet_rbl_t *rbl);

/**
 * clear a network read buffer list object.
 */
static inline void jetnet_rbl_clear(jetnet_rbl_t *rbl){
	rbl->head = NULL;
	rbl->tail = NULL;
	rbl->offset = 0;
	rbl->size = 0;
}

/**
 * append a read buffer to the read buffer list.
 */
void jetnet_rbl_appen(jetnet_rbl_t *rbl, jetnet_rb_t*rb);

/**
 * get the data length of this rbl.
 */
static inline int jetnet_rbl_size(jetnet_rbl_t *rbl){
	return rbl->size;
}

/**
 * read data from rbl,return the actual read byte count.
 */
int jetnet_rbl_read(jetnet_rbl_t *rbl, char*data, int len);

/**
 * pink data from rbl,return whether we get data.
 */
int jetnet_rbl_peek(jetnet_rbl_t *rbl, int offset, char*buf, int size);

/**
 * free an network write buffer object
 */
void jetnet_wb_free(jetnet_wb_t *wb);

/**
 * free an network write buffer list object
 */
void jetnet_wbl_free(jetnet_wbl_t *wbl);

/**
 * clear a network write buffer list object.
 */
static inline void jetnet_wbl_clear(jetnet_wbl_t *wbl){
	wbl->head = NULL;
	wbl->tail = NULL;
}

/**
 * append a write buffer to the read buffer list.
 */
void jetnet_wbl_appen(jetnet_wbl_t *wbl, jetnet_wb_t*wb);

/**
 * drop data from network write buffer list.
 */
void jetnet_wbl_drop(jetnet_wbl_t *wbl,int size);

/**
 * get io vector from write buffer list.
 */
int jetnet_wbl_get_iovec(jetnet_wbl_t *wbl, jetnet_iov_t*vec, int count, int*byte_cnt);

/**
 * calculate the size of network write buffer list.
 */
int jetnet_wbl_size(jetnet_wbl_t *wbl);

/**
 * check whether the network write buffer list is empty.
 */
static inline int jetnet_wbl_is_empty(jetnet_wbl_t *wbl){
	if(wbl->head)
		return 0;
	return 1;
}

/**
 * pop the head write buffer from a list.
 */
jetnet_wb_t* jetnet_wbl_pop_head(jetnet_wbl_t *wbl);


#if defined(__cplusplus)
}
#endif

#endif /* JETNET_IOBUF_H_INCLUDED */