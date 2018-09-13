#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "jetnet_api.h"
#include "rbtree.h"

#define API_EXP __attribute__((visibility("default")))

/*define the gate object type*/
#define APP_OBJ_TYPE_GATE 1

/*define the entity no for gate object*/
#define GATE_ENO	1


typedef struct app_obj_t{
	int type;
}app_obj_t;

typedef struct gate_socket_t{
	int socket_id; /*the socket id*/
	uint32_t usr_id;	
	jetnet_peer_t peer; /*the peer address*/
	int state; /*the socket connection state*/
	struct rb_node sid_node;
	struct rb_node uid_node;
}gate_socket_t;

typedef struct gate_t{
	app_obj_t base;
	struct rb_root sid_map;
	struct rb_root uid_map;
	int listen_sock_id;
}gate_t;

static jetnet_api_t* g_api = NULL;

static gate_socket_t* gate_find_by_sid(gate_t*gate, int sid){
  	struct rb_node *node = gate->sid_map.rb_node;
  	while (node) {
  		gate_socket_t *data = rb_entry(node, gate_socket_t, sid_node);
		if (sid < data->socket_id)
  			node = node->rb_left;
		else if (sid > data->socket_id)
  			node = node->rb_right;
		else
  			return data;
	}
	return NULL;	
}

static int gate_insert_by_sid(gate_t*gate, gate_socket_t*gs){
  	struct rb_node **new = &(gate->sid_map.rb_node), *parent = NULL;
  	/* Figure out where to put new node */
  	while (*new) {
  		gate_socket_t *this = rb_entry(*new, gate_socket_t, sid_node);
		parent = *new;
  		if (gs->socket_id < this->socket_id)
  			new = &((*new)->rb_left);
  		else if (gs->socket_id > this->socket_id)
  			new = &((*new)->rb_right);
  		else
  			return 0;
  	}
  	/* Add new node and rebalance tree. */
  	rb_link_node(&gs->sid_node, parent, new);
  	rb_insert_color(&gs->sid_node, &gate->sid_map);
	return 1;
}


static gate_socket_t* gate_find_by_uid(gate_t*gate, int uid){
  	struct rb_node *node = gate->uid_map.rb_node;
  	while (node) {
  		gate_socket_t *data = rb_entry(node, gate_socket_t, uid_node);
		if (uid < data->socket_id)
  			node = node->rb_left;
		else if (uid > data->socket_id)
  			node = node->rb_right;
		else
  			return data;
	}
	return NULL;	
}

static int gate_insert_by_uid(gate_t*gate, gate_socket_t*gs){
  	struct rb_node **new = &(gate->uid_map.rb_node), *parent = NULL;
  	/* Figure out where to put new node */
  	while (*new) {
  		gate_socket_t *this = rb_entry(*new, gate_socket_t, uid_node);
		parent = *new;
  		if (gs->socket_id < this->socket_id)
  			new = &((*new)->rb_left);
  		else if (gs->socket_id > this->socket_id)
  			new = &((*new)->rb_right);
  		else
  			return 0;
  	}
  	/* Add new node and rebalance tree. */
  	rb_link_node(&gs->uid_node, parent, new);
  	rb_insert_color(&gs->uid_node, &gate->uid_map);
	return 1;
}

static void gate_destory_socket_by_sid(gate_t*gate, struct rb_node* node){
	if(!node)
		return;
}

static void gate_destory_socket_by_uid(gate_t*gate, struct rb_node* node){
	if(!node)
		return;
}

static void gate_handle_conn(gate_t*gate, jetnet_cmd_connect_t*conn){
}

static void gate_handle_accept(gate_t*gate, jetnet_cmd_accept_t*acc){
}

static void gate_handle_recv(gate_t*gate, jetnet_cmd_recv_t*recv){
}

static void gate_handle_close(gate_t*gate, jetnet_cmd_close_t*close){
}

static void gate_handle_deliver(gate_t*gate, jetnet_msg_t*deliver_msg){
	
}

static int gate_handle_cmd_msg(gate_t*gate, jetnet_msg_t*msg){
	int cmd_id = *(int*)msg->msg_tag;
	switch(cmd_id){
		case CMDTAG_TEST:{
			jetnet_cmd_test_t*test = JETNET_GET_MSG_SUB(msg,jetnet_cmd_test_t);
			int cmd_len = sizeof(jetnet_cmd_test_t);
			int msg_len = JETNET_GET_VAR_LEN(jetnet_msg_t,cmd_len);
			jetnet_msg_t*echo_msg = (jetnet_msg_t*)g_api->malloc(msg_len);
			echo_msg->src_eid = msg->dst_eid;
			echo_msg->dst_eid = msg->src_eid;
			echo_msg->msg_seq_id = msg->msg_seq_id;
			echo_msg->msg_type = MSGT_SYS_CMD;
			MSGT_SET_CMD_TAG(echo_msg,CMDTAG_TEST);
			echo_msg->msg_len = cmd_len;
			jetnet_cmd_test_t*echo_test = (jetnet_cmd_test_t*)echo_msg->msg_data;
			memcpy(echo_test,test,sizeof(jetnet_cmd_test_t));
			g_api->post_msg(echo_msg);
			g_api->free(echo_msg);
			
			static unsigned long last_time = 0;
			static int recv_count = 0;
			static int last_count = 0;
			if(last_time == 0){
				last_time = g_api->time_now();
			}else{
				unsigned long now = g_api->time_now();
				if(now - last_time >= 1000){
					printf("recv msg %d per second!\n",recv_count-last_count);
					last_count = recv_count;
					last_time = now;
				}
			}				
			if(++recv_count == 1000000)
				g_api->exit();
			break;
		}
		case CMDTAG_CONNECT:{
			jetnet_cmd_connect_t*conn = JETNET_GET_MSG_SUB(msg,jetnet_cmd_connect_t);
			gate_handle_conn(gate, conn);
			break;
		}
		case CMDTAG_ACCEPT:{
			jetnet_cmd_accept_t*acc = JETNET_GET_MSG_SUB(msg,jetnet_cmd_accept_t);
			gate_handle_accept(gate, acc);
			break;
		}
		case CMDTAG_RECV:{
			jetnet_cmd_recv_t*recv = JETNET_GET_MSG_SUB(msg,jetnet_cmd_recv_t);
			gate_handle_recv(gate, recv);
			break;
		}
		case CMDTAG_CLOSE:{
			jetnet_cmd_close_t*close = JETNET_GET_MSG_SUB(msg,jetnet_cmd_close_t);
			gate_handle_close(gate, close);
			break;
		}
		case CMDTAG_DELIVER:{
			jetnet_msg_t*deliver_msg = JETNET_GET_MSG_SUB(msg,jetnet_msg_t);
			gate_handle_deliver(gate, deliver_msg);
			break;
		}		
	}
	return 1;
}

static int gate_msg_cb(jetnet_entity_t*entity, jetnet_msg_t*msg){
	//first,we should check the entity object must be the gate object
	app_obj_t*app_obj = (app_obj_t*)entity->ud;
	if(!app_obj)
		return 0;
	if(app_obj->type != APP_OBJ_TYPE_GATE)
		return 0;
	gate_t*gate = (gate_t*)app_obj;
	if(msg->msg_type == MSGT_SYS_CMD)
		return gate_handle_cmd_msg(gate, msg);
	return 1;
}

static void gate_udfree_func_t(jetnet_entity_t*entity){
	gate_t*gate = (gate_t*)entity->ud;
	gate_destory_socket_by_sid(gate, gate->sid_map.rb_node);
	gate_destory_socket_by_uid(gate, gate->uid_map.rb_node);
	if(gate->listen_sock_id != -1){
		g_api->net_close(gate->listen_sock_id);
		gate->listen_sock_id = -1;
	}
	g_api->free(gate);
}

API_EXP int gate_init(jetnet_api_t*api){
	if(g_api)
		return 0;
	//we create an entity object to hold the gate
	jetnet_entity_t*entity = api->create_entity(GATE_ENO);
	if(!entity)
		return 0;
	g_api = api;
	gate_t* gate = (gate_t*)g_api->malloc(sizeof(gate_t));
	gate->base.type = APP_OBJ_TYPE_GATE;
	gate->sid_map = RB_ROOT;
	gate->uid_map = RB_ROOT;
	gate->listen_sock_id = -1;
	//setup the entity info
	entity->msg_cb = &gate_msg_cb;
	entity->ud_free_func = &gate_udfree_func_t;
	entity->ud = gate;
	//we init the network
	jetnet_leid_t owner_id;
	owner_id.cno = entity->eid.cno;
	owner_id.eno = entity->eid.eno;
	gate->listen_sock_id = g_api->net_listen(owner_id,JETNET_PROTO_TCPSAC,
		"0.0.0.0",8686,102400);
	if(gate->listen_sock_id == -1){
		g_api->destroy_entity(entity);
		g_api = NULL;
		return 0;
	}
	return 1;
}

API_EXP void gate_startup(jetnet_api_t*api){
	if(!g_api)
		return;
	int cmd_len = sizeof(jetnet_cmd_test_t);
	int msg_len = JETNET_GET_VAR_LEN(jetnet_msg_t, cmd_len);
	jetnet_msg_t*echo_msg = (jetnet_msg_t*)g_api->malloc(msg_len);
	memset(echo_msg,0,msg_len);
	echo_msg->dst_eid.cip = g_api->cip;
	echo_msg->dst_eid.cport = g_api->cport;
	echo_msg->dst_eid.cno = g_api->hub_cno;
	echo_msg->dst_eid.eno = g_api->hub_eno;
	echo_msg->src_eid.cip = g_api->cip;
	echo_msg->src_eid.cport = g_api->cport;
	echo_msg->src_eid.cno = g_api->cno;
	echo_msg->src_eid.eno = GATE_ENO;	
	echo_msg->msg_type = MSGT_SYS_CMD;
	MSGT_SET_CMD_TAG(echo_msg,CMDTAG_TEST);
	echo_msg->msg_len = cmd_len;
	jetnet_cmd_test_t*test = JETNET_GET_MSG_SUB(echo_msg,jetnet_cmd_test_t);
	test->i = 100;
	strcpy(test->s,"hello,world!");
	g_api->post_msg(echo_msg);
	g_api->free(echo_msg);
}
