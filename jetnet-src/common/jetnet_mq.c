#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "jetnet_mq.h"
#include "jetnet_malloc.h"

#define JETNET_QUEUE_DEFAULT_SIZE 16

static void jetnet_mq_expand(jetnet_mq_t *mq) {
	jetnet_msg_t **new_queue = (jetnet_msg_t **)jetnet_malloc(sizeof(jetnet_msg_t*) * mq->cap * 2);
	int i;
	for (i=0;i<mq->cap;i++) {
		new_queue[i] = mq->queue[(mq->head + i) % mq->cap];
	}
	mq->head = 0;
	mq->tail = mq->cap;
	mq->cap *= 2;
	
	jetnet_free(mq->queue);
	mq->queue = new_queue;
}

void jetnet_mq_init(jetnet_mq_t *mq){
	mq->head = 0;
	mq->tail = 0;
	mq->cap = JETNET_QUEUE_DEFAULT_SIZE;
	mq->queue = (jetnet_msg_t **)jetnet_malloc(sizeof(jetnet_msg_t*) * mq->cap);
}

void jetnet_mq_release(jetnet_mq_t *mq){
	while(mq->head != mq->tail){
		jetnet_free(mq->queue[mq->head++]);
		if(mq->head >= mq->cap)
			mq->head = 0;
	}
	jetnet_free(mq->queue);
	mq->queue = NULL;
	mq->cap = 0;
	mq->head = mq->tail = 0;
}

void* jetnet_mq_pop(jetnet_mq_t *mq){
	jetnet_msg_t* ret = NULL;
	if (mq->head != mq->tail) {
		ret = mq->queue[mq->head++];
		if(mq->head >= mq->cap)
			mq->head = 0;
	}
	return ret;
}

int jetnet_mq_push(jetnet_mq_t *mq, jetnet_msg_t *msg){
	if(mq->cap == 0)
		return 0;
	mq->queue[mq->tail] = msg;
	if (++ mq->tail >= mq->cap) {
		mq->tail = 0;
	}

	if (mq->head == mq->tail) {
		jetnet_mq_expand(mq);
	}
	return 1;
}
