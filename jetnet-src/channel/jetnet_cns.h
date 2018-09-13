#ifndef __JETNET_CNS_H_INCLUDED
#define __JETNET_CNS_H_INCLUDED
#include "jetnet_msg.h"
#include "jetnet_sfifo.h"
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * init the share memory channels object.
 */
int jetnet_cns_init(uint32_t cn_id, unsigned int shm_size);

/**
 * destroy the share memory channels object.
 */
void jetnet_cns_release();

/**
 * add an output sff object to cns.
 * return values:
 *   -1 the sff object with the same key has exist.
 *   0 succeed.
 */
int jetnet_cns_add_output_sff(jetnet_sfifo_t * sff);

/**
 * put msg to a share memory channels object
 * return values:
 *   0 succeed
 *   -1 the channel han't enough space for put.
 *   -2 can't find the channel indicate by cn_id.
 */
int jetnet_cns_put_msg(jetnet_msg_t *msg);
 
 /**
 * put msg to a share memory channels object
 * return values:
 *   0 succeed
 *   -1 the channel han't enough space for put.
 *   -2 can't find the channel indicate by cn_id.
 */
int jetnet_cns_put_msg_var(jetnet_msg_t *msg, unsigned char*data, unsigned int len);

 /**
 * put msg to a share memory channels object
 * return values:
 *   0 succeed
 *   -1 the channel han't enough space for put.
 *   -2 can't find the channel indicate by cn_id.
 */
int jetnet_cns_put_msg_iov(jetnet_msg_t *msg, jetnet_fifo_iov_t* iov);


 /**
 * get msg from input channels if there are message arrive.
 */
jetnet_msg_t * jetnet_cns_get_msg();


#if defined(__cplusplus)
}
#endif

#endif /* __JETNET_CNS_H_INCLUDED */