#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "jetnet_ewrapper.h"
#include "jetnet_malloc.h"
#include "jetnet_const.h"
#include "list.h"
#include "rbtree.h"
#include "jetnet_mq.h"
#include "jetnet_timer.h"

#define	JETNET_HASH_WAIT_NUM	512

struct jetnet_ewrapper_t;
typedef struct jetnet_ewrapper_t jetnet_ewrapper_t;

#pragma pack(1)
typedef struct jetnet_wait_trigger_t{
	unsigned int wait_id;
	jetnet_wait_msg_cb cb;
	jetnet_msg_t*msg;
}jetnet_wait_trigger_t;
#pragma pack()

typedef struct jetnet_wait_item_t{
	unsigned int wait_id;
	jetnet_wait_msg_cb cb;
	jetnet_eid_t src_eid;
	uint32_t msg_seq_id;
	unsigned int timer_id;
	jetnet_ewrapper_t*ewrapper;
	struct list_head g_node; // group node
	struct hlist_node h_node; //hash node
}jetnet_wait_item_t;

typedef struct jetnet_entity_mgr_t{
	//local cell info
	uint32_t cip;
	uint16_t cport;
	uint16_t cno;
	//entity_id gener
	uint32_t entity_id_counter;
	//wait_id gener
	unsigned int wait_id_counter;
	//wait hash list heads
	struct hlist_head hash_waits[JETNET_HASH_WAIT_NUM];
	//entity tree root
	struct rb_root entity_root;
	//active entity link's head
	struct list_head active_list;
}jetnet_entity_mgr_t;

struct jetnet_ewrapper_t{
	jetnet_entity_t entity;
	jetnet_mq_t mq;
	jetnet_entity_mgr_t* entity_mgr;
	struct list_head wait_head;
	unsigned int block_wait_id;
	struct rb_node tnode;
	struct list_head active_node;
};

//the globe entity mgr object
jetnet_entity_mgr_t* g_entity_mgr = NULL;

static inline uint32_t jetnet_gen_entity_eno(uint32_t eno){
	if(!g_entity_mgr)
		return 0;
	if(eno == 0){
		while(1){
			while(!(++g_entity_mgr->entity_id_counter));
			if(!jetnet_entity_find(g_entity_mgr->entity_id_counter))
				return g_entity_mgr->entity_id_counter;
		}
	}	
	if(!jetnet_entity_find(eno))
		return eno;
	return 0;	
}

static inline unsigned int jetnet_gen_wait_id(){
	if(!g_entity_mgr)
		return 0;
	while(!(++g_entity_mgr->wait_id_counter));
	return g_entity_mgr->wait_id_counter;
}

static inline void jetnet_wrapper_free(jetnet_ewrapper_t*wrapper){
	jetnet_mq_release(&wrapper->mq);
	if(wrapper->entity.ud_free_func)
		wrapper->entity.ud_free_func(&wrapper->entity);
	jetnet_free(wrapper);
}

static void jetnet_wapper_destory_by_node(struct rb_node* node){
	if(!node)
		return;
	if(node->rb_left)
		jetnet_wapper_destory_by_node(node->rb_left);
	if(node->rb_right)
		jetnet_wapper_destory_by_node(node->rb_right);
	jetnet_ewrapper_t*wrapper = rb_entry(node, jetnet_ewrapper_t, tnode);
	jetnet_wrapper_free(wrapper);
}

static int jetnet_insert_entity(struct rb_root *root, jetnet_ewrapper_t *data){
  	struct rb_node **new = &(root->rb_node), *parent = NULL;
  	/* Figure out where to put new node */	
  	while (*new) {
  		jetnet_ewrapper_t *this = rb_entry(*new, jetnet_ewrapper_t, tnode);
		parent = *new;
  		if (data->entity.eid.eno < this->entity.eid.eno)
  			new = &((*new)->rb_left);
  		else if (data->entity.eid.eno > this->entity.eid.eno)
  			new = &((*new)->rb_right);
  		else
  			return 0;
  	}
  	/* Add new node and rebalance tree. */
  	rb_link_node(&data->tnode, parent, new);
  	rb_insert_color(&data->tnode, root);
	return 1;
}

static inline void jetnet_ewrapper_active(jetnet_ewrapper_t*ewarpper){
	if(!g_entity_mgr)
		return;
	if(list_empty(&ewarpper->active_node))
		list_add_tail(&ewarpper->active_node,&g_entity_mgr->active_list);
}

static inline void jetnet_ewrapper_deactive(jetnet_ewrapper_t*ewarpper){
	list_del_init(&ewarpper->active_node);
}

static inline int jetnet_ewrapper_isactive(jetnet_ewrapper_t*ewarpper){
	return list_empty(&ewarpper->active_node) ? 0 : 1;
}

static inline int jetnet_entity_wait_hash(jetnet_eid_t eid, uint32_t msg_seq_id){
	int v = (int)(eid.cip + eid.cport + eid.cno + eid.eno + msg_seq_id);
	return v%JETNET_HASH_WAIT_NUM;	
}

int jetnet_entity_mgr_init(uint32_t cip, uint16_t cport, uint16_t cno){
	if(g_entity_mgr)
		return 0;
	g_entity_mgr = (jetnet_entity_mgr_t*)jetnet_malloc(sizeof(jetnet_entity_mgr_t));
	g_entity_mgr->cip = cip;
	g_entity_mgr->cport = cport;
	g_entity_mgr->cno = cno;
	g_entity_mgr->entity_id_counter = 0;
	g_entity_mgr->wait_id_counter = 0;
	int i;
	for(i = 0; i < JETNET_HASH_WAIT_NUM; i++)
		INIT_HLIST_HEAD(&g_entity_mgr->hash_waits[i]);
	g_entity_mgr->entity_root = RB_ROOT;
	INIT_LIST_HEAD(&g_entity_mgr->active_list);
	return 1;
}

void jetnet_entity_mgr_release(){
	if(!g_entity_mgr)
		return;
	//free all wait items
	int i;
	jetnet_wait_item_t* item;
	struct hlist_node*hnode;
	struct hlist_node*temp;
	for(i = 0; i < JETNET_HASH_WAIT_NUM; i++){		
		hlist_for_each_entry_safe(item, hnode, temp, &g_entity_mgr->hash_waits[i], h_node){
			jetnet_free(item);
		}
	}
	//free all entity
	jetnet_wapper_destory_by_node(g_entity_mgr->entity_root.rb_node);
	//free self
	jetnet_free(g_entity_mgr);
	g_entity_mgr = NULL;
}

jetnet_entity_t* jetnet_entity_create(uint32_t eno){
	if(!g_entity_mgr)
		return NULL;
	uint32_t new_eno = jetnet_gen_entity_eno(eno);
	if(new_eno == 0)
		return NULL;
	jetnet_ewrapper_t*wrapper = (jetnet_ewrapper_t*)jetnet_malloc(sizeof(jetnet_ewrapper_t));
	wrapper->entity.eid.cip = g_entity_mgr->cip;
	wrapper->entity.eid.cport = g_entity_mgr->cport;
	wrapper->entity.eid.cno = g_entity_mgr->cno;
	wrapper->entity.eid.eno = new_eno;
	wrapper->entity.msg_cb = NULL;
	wrapper->entity.ud_free_func = NULL;
	wrapper->entity.ud = NULL;
	jetnet_mq_init(&wrapper->mq);
	wrapper->entity_mgr = g_entity_mgr;
	INIT_LIST_HEAD(&wrapper->wait_head);
	wrapper->block_wait_id = 0;
	memset(&wrapper->tnode,0,sizeof(wrapper->tnode));
	INIT_LIST_HEAD(&wrapper->active_node);
	jetnet_insert_entity(&g_entity_mgr->entity_root, wrapper);
	return &wrapper->entity;
}

jetnet_entity_t* jetnet_entity_find(uint32_t eno){
	if(!g_entity_mgr)
		return NULL;
  	struct rb_node *node = g_entity_mgr->entity_root.rb_node;
  	while (node) {
  		jetnet_ewrapper_t *data = rb_entry(node, jetnet_ewrapper_t, tnode);
		if (eno < data->entity.eid.eno)
  			node = node->rb_left;
		else if (eno > data->entity.eid.eno)
  			node = node->rb_right;
		else
  			return &data->entity;
	}
	return NULL;
}

void jetnet_entity_destory(jetnet_entity_t*entity){
	if(!g_entity_mgr)
		return;
	jetnet_ewrapper_t*wrapper = (jetnet_ewrapper_t*)entity;
	//remove all wait item belong this wrapper
	jetnet_wait_item_t* pos;
	jetnet_wait_item_t* n;
	list_for_each_entry_safe(pos, n, &wrapper->wait_head, g_node){
		//cancel timer
		jetnet_timer_kill(pos->timer_id);
		//remove from the hash list
		hlist_del(&pos->h_node);
		//delete item
		jetnet_free(pos);
	}
	//remove wrapper from active link
	if(!list_empty(&wrapper->active_node))
		list_del(&wrapper->active_node);
	//erase from the rbtree
	rb_erase(&wrapper->tnode, &g_entity_mgr->entity_root);
	//delete wapper
	jetnet_wrapper_free(wrapper);	
}

int jetnet_entity_push_msg(jetnet_msg_t*msg){
	if(!g_entity_mgr){
		return 1;
	}
	jetnet_msg_t*real_msg = msg;
	if(MSGT_IS_CMD(msg,CMDTAG_DELIVER))
		real_msg = JETNET_GET_MSG_SUB(msg,jetnet_msg_t);
	//find the wrapper object
	jetnet_ewrapper_t*wrapper = (jetnet_ewrapper_t*)jetnet_entity_find(real_msg->dst_eid.eno);
	if(!wrapper){
		return 1;
	}
	//we check whether the message is rpc ack
	if(HAS_MSGT_RPCACK_FLAG(real_msg->msg_type)){
		//check whether this msg is fit the wait msg
		int hash_value = jetnet_entity_wait_hash(real_msg->src_eid,real_msg->msg_seq_id);	
		int get = 0;
		jetnet_wait_item_t*tpos;
		struct hlist_node *pos;
		struct hlist_head*head = &g_entity_mgr->hash_waits[hash_value];
		pos = head->first;
		while(pos){
			tpos = hlist_entry(pos,jetnet_wait_item_t,h_node);
			if(tpos->msg_seq_id == real_msg->msg_seq_id && 
				tpos->src_eid.cip == real_msg->src_eid.cip && 
				tpos->src_eid.cport == real_msg->src_eid.cport && 
				tpos->src_eid.cno == real_msg->src_eid.cno && 
				tpos->src_eid.eno == real_msg->src_eid.eno){
				get = 1;
				break;
			}
			pos = pos->next;
		}
		if(!get){
			//get an rpc ack ,but can't find the info,we just free this message
			return 1;
		}
		//we should kill the timer first
		jetnet_timer_kill(tpos->timer_id);
		//remove from the hash list
		hlist_del(&tpos->h_node);
		//remove from the group list
		list_del(&tpos->g_node);
		//get call back info
		unsigned int wait_id = tpos->wait_id;
		jetnet_wait_msg_cb cb = tpos->cb;
		//delete the wait item
		jetnet_free(tpos);
		if(wrapper->block_wait_id == 0){					
			//call back
			if(cb)
				cb(wait_id,real_msg);
			return 1;
		}else{
			if(wait_id == wrapper->block_wait_id){
				//clean flag
				wrapper->block_wait_id = 0;
				//reactive
				if(!jetnet_ewrapper_isactive(wrapper)&&jetnet_mq_size(&wrapper->mq)>0)
					jetnet_ewrapper_active(wrapper);
				//call back
				if(cb)
					cb(wait_id,real_msg);
				return 1;
			}else{
				//we generate an wait_trigger msg,and put it to the queue
				unsigned int msg_len = JETNET_MSG_HEADER_LEN + sizeof(jetnet_wait_trigger_t);
				jetnet_msg_t*nmsg = (jetnet_msg_t*)jetnet_malloc(msg_len);
				nmsg->msg_type = MSGT_SYS_WAIT_TRIGGER;
				jetnet_wait_trigger_t*trigger = JETNET_GET_MSG_SUB(nmsg,jetnet_wait_trigger_t);
				trigger->wait_id = wait_id;
				trigger->cb = cb;
				trigger->msg = msg;
				//push to the queue
				jetnet_mq_push(&wrapper->mq,nmsg);
				return 0;
			}
		}
	}
	//push msg to queue
	jetnet_mq_push(&wrapper->mq,msg);
	//reactive
	if(0 == wrapper->block_wait_id && !jetnet_ewrapper_isactive(wrapper))
		jetnet_ewrapper_active(wrapper);
	return 0;
}

int jetnet_entity_pop_msg(jetnet_msg_t**msg, jetnet_entity_t**entity){
	*msg = NULL;
	*entity = NULL;
	if(!g_entity_mgr)
		return 0;
	struct list_head *head = list_pop_head(&g_entity_mgr->active_list);
	if(!head)
		return 0;
	jetnet_ewrapper_t* wrapper = list_entry(head, jetnet_ewrapper_t, active_node);	
	*msg = jetnet_mq_pop(&wrapper->mq);
	if(*msg){
		*entity = &wrapper->entity;
	}
	if(jetnet_mq_size(&wrapper->mq) > 0){
		jetnet_ewrapper_active(wrapper);
	}
	return *msg? 1 : 0;
}

static void jetnet_entity_wait_to_func(unsigned int timer_id, void*data, void*ud){
	jetnet_wait_item_t* item = (jetnet_wait_item_t*)data;
	jetnet_ewrapper_t* wrapper = item->ewrapper;
	//remove from the hash list
	hlist_del(&item->h_node);
	//remove from the group list
	list_del(&item->g_node);
	//get call back info
	jetnet_wait_msg_cb cb = item->cb;
	unsigned int wait_id = item->wait_id;
	//delete the wait item
	jetnet_free(item);
	
	if(wrapper->block_wait_id == 0){
		//call back
		if(cb)
			cb(wait_id,NULL);
	}else{
		if( wrapper->block_wait_id == wait_id ){
			wrapper->block_wait_id = 0;
			//reactive
			if(!jetnet_ewrapper_isactive(wrapper)&&jetnet_mq_size(&wrapper->mq)>0)
				jetnet_ewrapper_active(wrapper);
			//call back
			if(cb)
				cb(wait_id,NULL);
		}else{
			//we generate an wait_trigger msg,and put it to the queue
			unsigned int msg_len = JETNET_MSG_HEADER_LEN + sizeof(jetnet_wait_trigger_t);
			jetnet_msg_t*nmsg = (jetnet_msg_t*)jetnet_malloc(msg_len);
			nmsg->msg_type = MSGT_SYS_WAIT_TRIGGER;
			jetnet_wait_trigger_t*trigger = JETNET_GET_MSG_SUB(nmsg,jetnet_wait_trigger_t);
			trigger->wait_id = wait_id;
			trigger->cb = cb;
			trigger->msg = NULL;
			//push to the queue
			jetnet_mq_push(&wrapper->mq,nmsg);
		}
	}
}

unsigned int jetnet_entity_wait_msg(int mode, jetnet_entity_t*entity, jetnet_eid_t src_eid,
		uint32_t msg_seq_id, int time_out, jetnet_wait_msg_cb cb){
	if(!g_entity_mgr)
		return 0;
	jetnet_ewrapper_t*wrapper = (jetnet_ewrapper_t*)entity;
	jetnet_wait_item_t*item = (jetnet_wait_item_t*)jetnet_malloc(sizeof(jetnet_wait_item_t));
	item->wait_id = jetnet_gen_wait_id();
	item->cb = cb;
	item->src_eid = src_eid;
	item->msg_seq_id = msg_seq_id;
	item->timer_id = jetnet_timer_create(0,time_out, 1, jetnet_entity_wait_to_func, item, NULL);
	item->ewrapper = wrapper;
	INIT_LIST_HEAD(&item->g_node);
	INIT_HLIST_NODE(&item->h_node);
	//add to the entity group
	list_add_tail(&item->g_node, &wrapper->wait_head);
	int hash = jetnet_entity_wait_hash(src_eid, msg_seq_id);
	hlist_add_head(&item->h_node, &g_entity_mgr->hash_waits[hash]);
	//check whether we should deactive now
	if(mode == JETNET_WAIT_MODE_SYNC){
		wrapper->block_wait_id = item->wait_id;
		if(jetnet_ewrapper_isactive(wrapper))
			jetnet_ewrapper_deactive(wrapper);
	}
	return item->wait_id;
}

void	handle_wait_trigger_msg(jetnet_msg_t*msg){
	jetnet_wait_trigger_t*trigger = JETNET_GET_MSG_SUB(msg,jetnet_wait_trigger_t);
	if(trigger->cb)
		trigger->cb(trigger->wait_id,trigger->msg);
	if(trigger->msg)
		jetnet_free(trigger->msg);
	jetnet_free(msg);
}

