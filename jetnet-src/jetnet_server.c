#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jetnet_server.h"
#include "jetnet_cns.h"
#include "jetnet_ns.h"
#include "jetnet_cell_cfg.h"
#include "jetnet_time.h"
#include "jetnet_module.h"
#include "jetnet_api.h"
#include "jetnet_mq.h"
#include "jetnet_malloc.h"
#include "jetnet_timer.h"
#include "jetnet_ewrapper.h"
#include "jetnet_ultil.h"
#include <dlfcn.h>

typedef struct jetnet_server_t{
	jetnet_api_t base;
	int state;
}jetnet_server_t;

enum{
	JETNET_SERVER_STATE_STARTING=0,
	JETNET_SERVER_STATE_START,
};

jetnet_server_t* g_server = NULL;
int g_exit_flag = 0;

//////////////////////api///////////////////////////
static int jetnet_api_impl_open_module(const char*module_name){
	jetnet_module_t* module = jetnet_module_query(module_name);
	if(!module)
		return 0;
	return 1;
}

static uint32_t jetnet_api_impl_gen_seq_id(){
	static uint32_t g_seq_id_counter = 0;
	g_seq_id_counter++;
	if(g_seq_id_counter == 0)
		g_seq_id_counter = 1;
	return g_seq_id_counter;
}


/*note:we will not free the msg!!*/
static int jetnet_api_impl_post_msg(jetnet_msg_t*msg){
	if(!g_server)
		return 0;
	if(msg->dst_eid.cip == g_server->base.cip && msg->dst_eid.cport == g_server->base.cport){
		jetnet_msg_t deliver_msg;
		memset(&deliver_msg,0,sizeof(deliver_msg));
		deliver_msg.dst_eid = msg->dst_eid;
		deliver_msg.src_eid.cip = g_server->base.cip;
		deliver_msg.src_eid.cport = g_server->base.cport;
		deliver_msg.src_eid.cno = g_server->base.cno;
		deliver_msg.src_eid.eno = 0;
		deliver_msg.msg_type = MSGT_SYS_CMD;
		MSGT_SET_CMD_TAG(&deliver_msg,CMDTAG_DELIVER);
		deliver_msg.msg_len = JETNET_MSG_LEN(msg);
		jetnet_cns_put_msg_var(&deliver_msg, (unsigned char*)msg, JETNET_MSG_LEN(msg));
		return 1;
	}
	//remote msg ,we post it to huber
	jetnet_msg_t send_msg;
	memset(&send_msg,0,sizeof(send_msg));
	send_msg.dst_eid.cip = g_server->base.cip;
	send_msg.dst_eid.cport = g_server->base.cport;
	send_msg.dst_eid.cno = g_server->base.hub_cno;
	send_msg.dst_eid.eno = g_server->base.hub_eno;
	send_msg.src_eid.cip = g_server->base.cip;
	send_msg.src_eid.cport = g_server->base.cport;
	send_msg.src_eid.cno = g_server->base.cno;
	send_msg.src_eid.eno = 0;
	send_msg.msg_type = MSGT_SYS_CMD;
	MSGT_SET_CMD_TAG(&send_msg,CMDTAG_SNDMSG);
	send_msg.msg_len = JETNET_MSG_LEN(msg);
	jetnet_cns_put_msg_var(&send_msg, (unsigned char*)msg, JETNET_MSG_LEN(msg));
	return 1;
}

static int jetnet_api_impl_post_msg_var(jetnet_msg_t*msg,unsigned char*buf, unsigned int len){
	if(!g_server)
		return 0;
	if(msg->dst_eid.cip == g_server->base.cip && msg->dst_eid.cport == g_server->base.cport){
		jetnet_msg_t deliver_msg;
		memset(&deliver_msg,0,sizeof(deliver_msg));
		deliver_msg.dst_eid = msg->dst_eid;
		deliver_msg.src_eid.cip = g_server->base.cip;
		deliver_msg.src_eid.cport = g_server->base.cport;
		deliver_msg.src_eid.cno = g_server->base.cno;
		deliver_msg.src_eid.eno = 0;
		deliver_msg.msg_type = MSGT_SYS_CMD;
		MSGT_SET_CMD_TAG(&deliver_msg,CMDTAG_DELIVER);
		deliver_msg.msg_len = JETNET_MSG_LEN(msg);
		jetnet_fifo_iov_t iov;
		iov.size = 2;
		iov.vec[0].buf = msg;
		iov.vec[0].size = JETNET_MSG_HEADER_LEN;
		iov.vec[1].buf = buf;
		iov.vec[1].size = len;
		jetnet_cns_put_msg_iov(&deliver_msg, &iov);
		return 1;
	}
	//remote msg ,we post it to huber
	jetnet_msg_t send_msg;
	memset(&send_msg,0,sizeof(send_msg));
	send_msg.dst_eid.cip = g_server->base.cip;
	send_msg.dst_eid.cport = g_server->base.cport;
	send_msg.dst_eid.cno = g_server->base.hub_cno;
	send_msg.dst_eid.eno = g_server->base.hub_eno;
	send_msg.src_eid.cip = g_server->base.cip;
	send_msg.src_eid.cport = g_server->base.cport;
	send_msg.src_eid.cno = g_server->base.cno;
	send_msg.src_eid.eno = 0;
	send_msg.msg_type = MSGT_SYS_CMD;
	MSGT_SET_CMD_TAG(&send_msg,CMDTAG_SNDMSG);
	send_msg.msg_len = JETNET_MSG_LEN(msg);
	jetnet_fifo_iov_t iov;
	iov.size = 2;
	iov.vec[0].buf = msg;
	iov.vec[0].size = JETNET_MSG_HEADER_LEN;
	iov.vec[1].buf = buf;
	iov.vec[1].size = len;
	jetnet_cns_put_msg_iov(&send_msg, &iov);
	return 1;
}

static unsigned int jetnet_api_impl_wait(int mode, jetnet_entity_t*entity, jetnet_eid_t src_eid,
		uint32_t msg_seq_id, int time_out, jetnet_wait_msg_cb cb){
	return jetnet_entity_wait_msg(mode,entity, src_eid, msg_seq_id, time_out, cb);
}

static unsigned int jetnet_api_impl_timer_create(int delay,int period,
	int repeat, jetnet_timer_cb cb, void*data, void*ud){
	return jetnet_timer_create(delay, period, repeat, cb, data, ud);
}

static void jetnet_api_impl_timer_kill(unsigned int timer_id){
	jetnet_timer_kill(timer_id);
}

static void* jetnet_api_impl_malloc(size_t sz){
	void*p = jetnet_malloc(10);
	jetnet_free(p);
	return jetnet_malloc(sz);
}

static void jetnet_api_impl_free(void*p){
	jetnet_free(p);
}

void jetnet_api_impl_exit(){
	g_exit_flag = 1;
}

int jetnet_server_init(const char*cfg_file, const char*cell_name){
	if(g_server)
		return 0;
	//load config
	jetnet_cell_cfg_t* cell_cfg = jetnet_load_cell_cfg(cfg_file);
	if(!cell_cfg)
		return 0;
	jetnet_cell_cfg_item_t*cell_item = jetnet_find_cell_cfg(cell_cfg, cell_name);
	if(!cell_item){
		jetnet_free_cell_cfg(cell_cfg);
		return 0;
	}
	g_server = (jetnet_server_t*)jetnet_malloc(sizeof(jetnet_server_t));
	memset(g_server,0,sizeof(g_server));	
	g_server->base.cip = cell_item->cip;
	g_server->base.cport = cell_item->cport;
	g_server->base.cno = cell_item->cno;
	g_server->base.hub_cno = cell_item->hub_cno;
	g_server->base.hub_eno = cell_item->hub_eno;
	strncpy(g_server->base.cell_name,cell_item->cell_name,sizeof(g_server->base.cell_name)-1);
	g_server->state = JETNET_SERVER_STATE_STARTING;
	//init api table
	g_server->base.open_module = &jetnet_api_impl_open_module;
	g_server->base.gen_seq_id = &jetnet_api_impl_gen_seq_id;	
	g_server->base.create_entity = &jetnet_entity_create;
	g_server->base.destroy_entity = &jetnet_entity_destory;
	g_server->base.find_entity = &jetnet_entity_find;
	g_server->base.malloc = &jetnet_api_impl_malloc;
	g_server->base.free = &jetnet_api_impl_free;
	g_server->base.net_listen = &jetnet_net_listen;
	g_server->base.net_connect = &jetnet_net_connect;
	g_server->base.net_send = &jetnet_net_send;
	g_server->base.net_close = &jetnet_net_close;
	g_server->base.is_connected = &jetnet_net_is_connected;
	g_server->base.post_msg = &jetnet_api_impl_post_msg;
	g_server->base.post_msg_var = &jetnet_api_impl_post_msg_var;
	g_server->base.wait = &jetnet_api_impl_wait;
	g_server->base.time_now = &jetnet_time_now;
	g_server->base.timer_create = &jetnet_api_impl_timer_create;
	g_server->base.timer_kill = &jetnet_api_impl_timer_kill;
	g_server->base.ip_s2i = &jetnet_ip_s2i;
	g_server->base.ip_i2s = &jetnet_ip_i2s;
	g_server->base.exit = &jetnet_api_impl_exit;
	
	if(!jetnet_timer_mgr_init()){
		jetnet_server_release();
		jetnet_free_cell_cfg(cell_cfg);
		return 0;
	}

	if(!jetnet_cns_init(cell_item->cno, cell_item->channel_size)){
		jetnet_server_release();
		jetnet_free_cell_cfg(cell_cfg);
		return 0;
	}
	
	if(!jetnet_ns_init()){
		jetnet_server_release();
		jetnet_free_cell_cfg(cell_cfg);
		return 0;
	}
	
	if(!jetnet_entity_mgr_init(cell_item->cip,cell_item->cport,cell_item->cno)){
		jetnet_server_release();
		jetnet_free_cell_cfg(cell_cfg);
		return 0;
	}
	
	if(!jetnet_module_init(cell_item->module_path)){
		jetnet_server_release();
		jetnet_free_cell_cfg(cell_cfg);
		return 0;
	}
	//load modules
	int i;
	for(i = 0; i < MAX_MODULE_TYPE; i++){
		if(!cell_item->modules[i])
			break;
		if(!jetnet_module_query(cell_item->modules[i])){
			printf("load module %s fail!\n",cell_item->modules[i]);
			jetnet_server_release();
			jetnet_free_cell_cfg(cell_cfg);
			return 0;
		}
	}
	g_server->state = JETNET_SERVER_STATE_START;	
	//init all modules
	int module_num = jetnet_module_get_size();
	for(i = 0; i < module_num; i++){
		jetnet_module_t* mdl = jetnet_module_get(i);
		if(mdl->dl_init){
			if(!mdl->dl_init(&g_server->base)){
				printf("init module %s fail!\n",mdl->name);
				return 0;
			}
		}
	}	
	for(i = 0; i < module_num; i++){
		jetnet_module_t* mdl = jetnet_module_get(i);
		if(mdl->dl_startup)
			mdl->dl_startup(&g_server->base);
	}
	jetnet_free_cell_cfg(cell_cfg);
	return 1;
}
	
void jetnet_server_release(jetnet_server_t*server){
	jetnet_entity_mgr_release();
	jetnet_module_release();
	jetnet_cns_release();
	jetnet_ns_release();
	jetnet_timer_mgr_release();
	if(g_server){
		jetnet_free(g_server);
		g_server = NULL;
	}
}

void jetnet_server_loop(){
	if(!g_server)
		return;
	jetnet_mq_t mq;
	jetnet_mq_init(&mq);
	jetnet_entity_t* entity;
	jetnet_msg_t*msg;
	int timeout = 1;
	while(!g_exit_flag){
		jetnet_time_invalid();
		msg = jetnet_cns_get_msg();
		if(msg){
			jetnet_mq_push(&mq, msg);
			timeout = 0;
		}
		jetnet_net_poll(&mq, timeout);
		if(timeout > 0)
			jetnet_time_invalid();
		if(jetnet_mq_size(&mq) > 0)
			timeout = 0;
		else
			timeout = 1;

		while((msg=jetnet_mq_pop(&mq))){
			if(jetnet_entity_push_msg(msg))
				jetnet_free(msg);
		}
		while(jetnet_entity_pop_msg(&msg, &entity)){
			if(msg->msg_type == MSGT_SYS_WAIT_TRIGGER){
				handle_wait_trigger_msg(msg);
				continue;
			}
			if(entity->msg_cb){
				jetnet_msg_t*real_msg = msg;
				if(MSGT_IS_CMD(msg,CMDTAG_DELIVER))
					real_msg = JETNET_GET_MSG_SUB(msg,jetnet_msg_t);
				entity->msg_cb(entity,real_msg);
			}
			jetnet_free(msg);
		}
	}
	jetnet_mq_release(&mq);
}
