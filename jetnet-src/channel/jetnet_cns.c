#include <string.h>
#include <stdlib.h>
#include "jetnet_cns.h"
#include "jetnet_sfifo.h"
#include "jetnet_malloc.h"
#include "rbtree.h"

#define INIT_CN_BUFFER_SIZE		65536

typedef struct jetnet_shm_kfifo_node_t{
	jetnet_sfifo_t* sff;
	struct rb_node node;
}jetnet_shm_kfifo_node_t;

typedef struct jetnet_shm_cns_t{
	jetnet_sfifo_t* input_fifo;
	struct rb_root output_fifos;
	unsigned int recv_buffer_size;
	unsigned char* recv_buffer;
}jetnet_shm_cns_t;

jetnet_shm_cns_t* g_cns = NULL;

static jetnet_sfifo_t * jetnet_shm_cns_find_sff(struct rb_root *root, uint32_t cn_id){
  	struct rb_node *node = root->rb_node;
  	while (node) {
  		jetnet_shm_kfifo_node_t *data = container_of(node, jetnet_shm_kfifo_node_t, node);
		if (cn_id < data->sff->key)
  			node = node->rb_left;
		else if (cn_id > data->sff->key)
  			node = node->rb_right;
		else
  			return data->sff;
	}
	return NULL;
}

static void jetnet_shm_cns_destroy_sff_node(struct rb_node*node){
	if(!node)
		return;
	if(node->rb_left)
		jetnet_shm_cns_destroy_sff_node(node->rb_left);
	if(node->rb_right)
		jetnet_shm_cns_destroy_sff_node(node->rb_right);
	jetnet_shm_kfifo_node_t*sff_node = rb_entry(node, jetnet_shm_kfifo_node_t, node);
	jetnet_shm_kfifo_close(sff_node->sff);
	jetnet_free((void*)sff_node);
}

int jetnet_cns_init(uint32_t cn_id, unsigned int shm_size){
	if(g_cns)
		return 0;
	jetnet_sfifo_t* sff = jetnet_shm_kfifo_create((key_t)cn_id, shm_size);
	if(!sff)
		return 0;	
	g_cns = (jetnet_shm_cns_t*)jetnet_malloc(sizeof(jetnet_shm_cns_t));
	g_cns->recv_buffer_size = INIT_CN_BUFFER_SIZE;
	g_cns->recv_buffer = (unsigned char*)jetnet_malloc(g_cns->recv_buffer_size);
	g_cns->input_fifo = sff;
	g_cns->output_fifos = RB_ROOT;
	return 1;
}

void jetnet_cns_release(){
	if(!g_cns)
		return;
	jetnet_shm_kfifo_close(g_cns->input_fifo);	
	jetnet_shm_cns_destroy_sff_node(g_cns->output_fifos.rb_node);	
	jetnet_free(g_cns->recv_buffer);
	jetnet_free(g_cns);
	g_cns = NULL;
}

int jetnet_cns_add_output_sff(jetnet_sfifo_t * sff){
	struct rb_node **new = &(g_cns->output_fifos.rb_node), *parent = NULL;
  	/* Figure out where to put new node */
  	while (*new) {
  		jetnet_shm_kfifo_node_t *this = container_of(*new, jetnet_shm_kfifo_node_t, node);
		parent = *new;
  		if (sff->key < this->sff->key)
  			new = &((*new)->rb_left);
  		else if (sff->key > this->sff->key)
  			new = &((*new)->rb_right);
  		else
  			return -1;
  	}
	jetnet_shm_kfifo_node_t*sff_node = (jetnet_shm_kfifo_node_t*)jetnet_malloc(sizeof(jetnet_shm_kfifo_node_t));
	sff_node->sff = sff;
	memset(&sff_node->node, 0, sizeof(sff_node->node));
  	/* Add new node and rebalance tree. */
  	rb_link_node(&sff_node->node, parent, new);
  	rb_insert_color(&sff_node->node, &g_cns->output_fifos);
	return 0;
}

int jetnet_cns_put_msg(jetnet_msg_t *msg){
	if(!g_cns)
		return -2;
	uint32_t cn_id = (uint32_t)msg->dst_eid.cno;
	jetnet_sfifo_t * sff = NULL;
	if(g_cns->input_fifo && cn_id == g_cns->input_fifo->key)
		sff = g_cns->input_fifo;
	else
		sff = jetnet_shm_cns_find_sff(&g_cns->output_fifos, cn_id);
	if(!sff){
		sff = jetnet_shm_kfifo_open((key_t)cn_id);
		if(!sff)
			return -2;
		jetnet_cns_add_output_sff(sff);
	}	
	unsigned int len = JETNET_MSG_LEN(msg);
	return kfifo_pkg_put(sff->fifo,(unsigned char *)msg, len);
}

int jetnet_cns_put_msg_var(jetnet_msg_t *msg, unsigned char*data, unsigned int len){
	if(!g_cns)
		return -2;
	uint32_t cn_id = (uint32_t)msg->dst_eid.cno;
	jetnet_sfifo_t * sff = NULL;
	if(g_cns->input_fifo && cn_id == g_cns->input_fifo->key)
		sff = g_cns->input_fifo;
	else
		sff = jetnet_shm_cns_find_sff(&g_cns->output_fifos, cn_id);
	if(!sff){
		sff = jetnet_shm_kfifo_open((key_t)cn_id);
		if(!sff)
			return -2;
		jetnet_cns_add_output_sff(sff);
	}
	jetnet_fifo_iov_t iov;
	iov.size = 2;
	iov.vec[0].buf = msg;
	iov.vec[0].size = JETNET_MSG_HEADER_LEN;
	iov.vec[1].buf = data;
	iov.vec[1].size = len;
	return kfifo_pkg_put_iov(sff->fifo,&iov);
}

int jetnet_cns_put_msg_iov(jetnet_msg_t *msg, jetnet_fifo_iov_t* iov){
	if(!g_cns)
		return -2;
	uint32_t cn_id = (uint32_t)msg->dst_eid.cno;
	jetnet_sfifo_t * sff = NULL;
	if(g_cns->input_fifo && cn_id == g_cns->input_fifo->key)
		sff = g_cns->input_fifo;
	else
		sff = jetnet_shm_cns_find_sff(&g_cns->output_fifos, cn_id);
	if(!sff){
		sff = jetnet_shm_kfifo_open((key_t)cn_id);
		if(!sff)
			return -2;
		jetnet_cns_add_output_sff(sff);
	}	
	jetnet_fifo_iov_t _iov;
	int i;
	for(i = iov->size; i > 0; i--)
		_iov.vec[i] = iov->vec[i-1];
	_iov.vec[0].buf = msg;
	_iov.vec[0].size = JETNET_MSG_HEADER_LEN;
	_iov.size = iov->size + 1;
	return kfifo_pkg_put_iov(sff->fifo,&_iov);
}

jetnet_msg_t * jetnet_cns_get_msg(){
	if(!g_cns)
		return NULL;
	int ret = 0;
	unsigned int msg_len = g_cns->recv_buffer_size;
	while(1){
		ret = kfifo_pkg_get(g_cns->input_fifo->fifo,g_cns->recv_buffer,&msg_len);
		if(ret == -1)
			return NULL;
		if(ret == 0){
			jetnet_msg_t* msg = (jetnet_msg_t*)jetnet_malloc(msg_len);
			memcpy(msg,g_cns->recv_buffer,msg_len);
			return msg;
		}
		//expand the recv space
		unsigned int new_size = 0;
		for(new_size = g_cns->recv_buffer_size<<1;new_size < msg_len;new_size = new_size<<1);
		unsigned char* new_buffer = jetnet_malloc(new_size);
		if(!new_buffer)
			return NULL;
		jetnet_free(g_cns->recv_buffer);
		g_cns->recv_buffer = new_buffer;
		g_cns->recv_buffer_size = new_size;
	}
	return NULL;
}
