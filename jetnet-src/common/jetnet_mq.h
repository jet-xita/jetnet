#ifndef __JETNET_MQ_H_INCLUDED
#define __JETNET_MQ_H_INCLUDED

#include "jetnet_msg.h"

#if defined(__cplusplus)
extern "C" {
#endif

/*define a message queue*/
typedef struct jetnet_mq_t{
	int cap;
	int head;
	int tail;
	jetnet_msg_t **queue;
}jetnet_mq_t;

extern void jetnet_mq_init(jetnet_mq_t *mq);
extern void jetnet_mq_release(jetnet_mq_t *mq);
extern void* jetnet_mq_pop(jetnet_mq_t *mq);
extern int jetnet_mq_push(jetnet_mq_t *mq, jetnet_msg_t *msg);

static inline int jetnet_mq_size(jetnet_mq_t *mq){
	if (mq->head <= mq->tail) {
		return mq->tail - mq->head;
	}
	return mq->tail + mq->cap - mq->head;
}

#if defined(__cplusplus)
}
#endif

#endif /* __JETNET_MQ_H_INCLUDED */
