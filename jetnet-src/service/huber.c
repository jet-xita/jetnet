#include "jetnet_api.h"
#include "rbtree.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define API_EXP __attribute__((visibility("default")))

/*define the huber object type*/
#define APP_OBJ_TYPE_HUBER 1

/*define the entity no for huber object*/
#define HUBER_ENO	1
/*how many ms we should delay reconnect while encounter an connect fail*/
#define HUBER_DELAY_INTERVAL 1000 
/*define the max time to stop reconnect after last request*/
#define HUBER_STOP_RECONN_TIME  60000

enum{
	HUBER_DISCONNECTED = 0,
	HUBER_CONNECTING,
	HUBER_CONNECTED,
};

enum{
	HUBER_HEALTH_OK = 0,
	HUBER_HEALTH_UNKNOW,
	HUBER_HEALTH_FAIL,
};

enum{
	HUBER_DIRECT_IN = 0,
	HUBER_DIRECT_OUT,
};

typedef struct huber_socket_t{
	int socket_id; /*the socket id*/
	jetnet_peer_t peer; /*the peer address*/
	int state; /*the socket connection state*/
	int health; /*the socket health state*/
	int direct; /*the direction of connection*/
	int reconnect_num; /*reconnect times*/
	unsigned int reconn_timer_id; /*the reconnect timer id*/
	unsigned long last_request_time; /*the time last request*/
	unsigned long last_disconn_time; /*the time last disconnect*/
	struct rb_node sid_node;
	struct rb_node peer_node;
}huber_socket_t;

typedef struct app_obj_t{
	int type;
}app_obj_t;

typedef struct huber_t{
	app_obj_t base;
	struct rb_root sid_map;
	struct rb_root peer_map;
	int listen_sock_id;
}huber_t;

static jetnet_api_t* g_api = NULL;

static void huber_reconnect_timer_cb(unsigned int timer_id, void*data, void*ud);

static huber_socket_t* huber_find_by_sid(huber_t*huber, int sid){
  	struct rb_node *node = huber->sid_map.rb_node;
  	while (node) {
  		huber_socket_t *data = rb_entry(node, huber_socket_t, sid_node);
		if (sid < data->socket_id)
  			node = node->rb_left;
		else if (sid > data->socket_id)
  			node = node->rb_right;
		else
  			return data;
	}
	return NULL;	
}

static int huber_insert_by_sid(huber_t*huber, huber_socket_t*hs){
  	struct rb_node **new = &(huber->sid_map.rb_node), *parent = NULL;
  	/* Figure out where to put new node */
  	while (*new) {
  		huber_socket_t *this = rb_entry(*new, huber_socket_t, sid_node);
		parent = *new;
  		if (hs->socket_id < this->socket_id)
  			new = &((*new)->rb_left);
  		else if (hs->socket_id > this->socket_id)
  			new = &((*new)->rb_right);
  		else
  			return 0;
  	}
  	/* Add new node and rebalance tree. */
  	rb_link_node(&hs->sid_node, parent, new);
  	rb_insert_color(&hs->sid_node, &huber->sid_map);
	return 1;
}

static int huber_peer_cmp(jetnet_peer_t a, jetnet_peer_t b){
	if( a.cip > b.cip)
		return 1;
	if(a.cip < b.cip)
		return -1;
	if(a.cport > b.cport)
		return 1;
	if(a.cport < b.cport)
		return -1;
	return 0;
}

static huber_socket_t* huber_find_by_peer(huber_t*huber, jetnet_peer_t peer){
  	struct rb_node *node = huber->peer_map.rb_node;
  	while (node) {
  		huber_socket_t *data = rb_entry(node, huber_socket_t, peer_node);
		if ( huber_peer_cmp(peer,data->peer) < 0 )
  			node = node->rb_left;
		else if (huber_peer_cmp(peer,data->peer) > 0)
  			node = node->rb_right;
		else
  			return data;
	}
	return NULL;
}

static int huber_insert_by_peer(huber_t*huber, huber_socket_t*hs){
  	struct rb_node **new = &(huber->peer_map.rb_node), *parent = NULL;
	int result;
  	/* Figure out where to put new node */	
  	while (*new) {
  		huber_socket_t *this = rb_entry(*new, huber_socket_t, peer_node);
		parent = *new;
		result = huber_peer_cmp(hs->peer, this->peer);
  		if (result < 0)
  			new = &((*new)->rb_left);
  		else if (result > 0)
  			new = &((*new)->rb_right);
  		else
  			return 0;
  	}
  	/* Add new node and rebalance tree. */
  	rb_link_node(&hs->peer_node, parent, new);
  	rb_insert_color(&hs->peer_node, &huber->peer_map);
	return 1;	
}

static inline int huber_get_delay(int recon_num){
	int delay = (1<<(recon_num-1))*HUBER_DELAY_INTERVAL;
	return delay < 16000 ? delay:16000;
}

static void huber_hanclle_socket_fail(huber_t*huber, huber_socket_t* hs, int socket_id){	
	//close the socket
	if(socket_id != -1)
		g_api->net_close(socket_id);
	rb_erase(&hs->sid_node,&huber->sid_map);
	//do reconnect work
	unsigned long now = g_api->time_now();
	hs->state = HUBER_DISCONNECTED;
	hs->reconnect_num++;
	hs->health = HUBER_HEALTH_FAIL;
	hs->last_disconn_time = now;
	//check whether we should reconnect
	unsigned long diff = now >= hs->last_request_time?
		now-hs->last_request_time:((unsigned long)-1 - hs->last_request_time + now);	
	if( diff < HUBER_STOP_RECONN_TIME ){
		hs->reconn_timer_id = g_api->timer_create(0,diff,1,&huber_reconnect_timer_cb,huber,hs);
	}
}

static void huber_reconnect_timer_cb(unsigned int timer_id, void*data, void*ud){
	huber_t*huber = (huber_t*)data;
	huber_socket_t* hs = (huber_socket_t*)ud;
	//reset the timer_id
	hs->reconn_timer_id = 0;
	jetnet_entity_t*entity = JETNET_GET_ENTITY_BY_UD((void*)huber);
	struct in_addr addr;
	addr.s_addr = hs->peer.cip;		
	char*host = inet_ntoa(addr);
	jetnet_leid_t owner_id;
	owner_id.cno = entity->eid.cno;
	owner_id.eno = entity->eid.eno;
	int socket_id = g_api->net_connect(owner_id,JETNET_PROTO_TCPSAC,host,hs->peer.cport);
	if(socket_id == -1){
		huber_hanclle_socket_fail(huber, hs, -1);
	}else{
		hs->socket_id = socket_id;
		//insert
		huber_insert_by_sid(huber, hs);
		if(g_api->is_connected(socket_id)){
			hs->state = HUBER_CONNECTED;
			hs->reconnect_num = 0;
			hs->health = HUBER_HEALTH_OK;
			hs->last_disconn_time = 0;
		}
	}
}

static inline unsigned long huber_diff_time(unsigned long now, unsigned long last){
	if(now >= last)
		return now - last;
	return (unsigned long)(-1) - last + now;
}

static void huber_handle_conn(huber_t*huber, jetnet_cmd_connect_t*conn){
	huber_socket_t* hs = huber_find_by_sid(huber, conn->socket_id);
	if(!hs){
		g_api->net_close(conn->socket_id);
		return;
	}
	if(conn->result == 1){
		hs->state = HUBER_CONNECTED;
		hs->reconnect_num = 0;
		hs->health = HUBER_HEALTH_OK;
		hs->last_disconn_time = 0;
		return;
	}
	huber_hanclle_socket_fail(huber, hs, conn->socket_id);
}

static void huber_handle_accept(huber_t*huber, jetnet_cmd_accept_t*acc){
	huber_socket_t* hs = (huber_socket_t*)g_api->malloc(sizeof(huber_socket_t));
	hs->socket_id = acc->socket_id;
	hs->peer.cip = g_api->ip_s2i(acc->peer_host);
	hs->peer.cport = acc->peer_port;
	hs->state = HUBER_CONNECTED;
	hs->health = HUBER_HEALTH_OK;
	hs->direct = HUBER_DIRECT_IN;
	hs->reconnect_num = 0;
	hs->reconn_timer_id = 0;
	hs->last_request_time = 0;
	hs->last_disconn_time = 0;
	memset(&hs->sid_node,0,sizeof(hs->sid_node));
	memset(&hs->peer_node,0,sizeof(hs->peer_node));
	//insert
	huber_insert_by_peer(huber, hs);
}

static void huber_handle_recv(huber_t*huber, jetnet_cmd_recv_t*recv){
	jetnet_msg_t*msg = (jetnet_msg_t*)recv->data;
	jetnet_entity_t*entity = JETNET_GET_ENTITY_BY_UD((void*)huber);	
	if(msg->dst_eid.cip != entity->eid.cip || msg->dst_eid.cport != entity->eid.cport)
		return;
	g_api->post_msg(msg);
}

static void huber_handle_close(huber_t*huber, jetnet_cmd_close_t*close){
	jetnet_entity_t*entity = JETNET_GET_ENTITY_BY_UD((void*)huber);
	g_api->net_close(close->socket_id);
	if(close->is_listener){
		huber->listen_sock_id = -1;
		//the listen socket is close unexcept,we restart it
		const char*host = g_api->ip_i2s(entity->eid.cip);
		jetnet_leid_t owner_id;
		owner_id.cno = entity->eid.cno;
		owner_id.eno = entity->eid.eno;
		huber->listen_sock_id = g_api->net_listen(owner_id,JETNET_PROTO_TCPSAC,host,entity->eid.cport,102400);
		if(huber->listen_sock_id == -1){
			//show error message
		}
		return;
	}
	huber_socket_t* hs = huber_find_by_sid(huber, close->socket_id);
	if(!hs)
		return;
	huber_hanclle_socket_fail(huber, hs, close->socket_id);
}

static void huber_handle_send_msg(huber_t*huber, jetnet_msg_t*msg){
	jetnet_entity_t*entity = JETNET_GET_ENTITY_BY_UD((void*)huber);
	jetnet_peer_t peer;
	peer.cip = msg->dst_eid.cip;
	peer.cport = msg->dst_eid.cport;
	huber_socket_t*hs = huber_find_by_peer(huber, peer);
	if(!hs){
		jetnet_leid_t owner_id;
		owner_id.cno = entity->eid.cno;
		owner_id.eno = entity->eid.eno;
		const char* host = g_api->ip_i2s(msg->dst_eid.cip);
		int socket_id = g_api->net_connect(owner_id,JETNET_PROTO_TCPSAC,host,msg->dst_eid.cport);
		if(socket_id == -1)
			return;		
		hs = (huber_socket_t*)g_api->malloc(sizeof(huber_socket_t));
		memset(hs,0,sizeof(huber_socket_t));
		hs->socket_id = socket_id;
		hs->peer.cip = msg->dst_eid.cip;
		hs->peer.cport = msg->dst_eid.cport;
		if(g_api->is_connected(socket_id)){
			hs->state = HUBER_CONNECTED;
			hs->health = HUBER_HEALTH_OK;
		}else{
			hs->state = HUBER_CONNECTING;
			hs->health = HUBER_HEALTH_UNKNOW;
		}
		hs->direct = HUBER_DIRECT_OUT;
		huber_insert_by_sid(huber, hs);
		huber_insert_by_peer(huber, hs);
	}
	hs->last_request_time = g_api->time_now();
	if(hs->state == HUBER_HEALTH_FAIL)
		return;
	int msg_len = JETNET_MSG_LEN(msg);
	jetnet_msg_t*clone = (jetnet_msg_t*)g_api->malloc(msg_len);
	memcpy(clone,msg,msg_len);
	jetnet_leid_t owner_id;
	owner_id.cno = msg->src_eid.cno;
	owner_id.eno = msg->src_eid.eno;	
	g_api->net_send(hs->socket_id,owner_id,msg->msg_seq_id,clone,msg_len);
}

static int huber_handle_cmd_msg(huber_t*huber, jetnet_msg_t*msg){
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
			huber_handle_conn(huber, conn);
			break;
		}
		case CMDTAG_ACCEPT:{
			jetnet_cmd_accept_t*acc = JETNET_GET_MSG_SUB(msg,jetnet_cmd_accept_t);
			huber_handle_accept(huber, acc);
			break;
		}
		case CMDTAG_RECV:{
			jetnet_cmd_recv_t*recv = JETNET_GET_MSG_SUB(msg,jetnet_cmd_recv_t);
			huber_handle_recv(huber, recv);
			break;
		}
		case CMDTAG_CLOSE:{
			jetnet_cmd_close_t*close = JETNET_GET_MSG_SUB(msg,jetnet_cmd_close_t);
			huber_handle_close(huber, close);
			break;
		}
		case CMDTAG_SNDMSG:{
			jetnet_msg_t*inner_msg = JETNET_GET_MSG_SUB(msg,jetnet_msg_t);
			huber_handle_send_msg(huber,inner_msg);
			break;
		}
	}
	return 1;
}

static int huber_msg_cb(jetnet_entity_t*entity, jetnet_msg_t*msg){
	//first,we should check the entity object must be the huber object
	app_obj_t*app_obj = (app_obj_t*)entity->ud;
	if(!app_obj)
		return 0;
	if(app_obj->type != APP_OBJ_TYPE_HUBER)
		return 0;
	huber_t*huber = (huber_t*)app_obj;
	if(msg->msg_type == MSGT_SYS_CMD)//now only handle the cmd message
		return huber_handle_cmd_msg(huber, msg);
	return 1;
}

static void huber_destory_socket_by_sid(huber_t*huber, struct rb_node* node){
	if(!node)
		return;
	if(node->rb_left)
		huber_destory_socket_by_sid(huber,node->rb_left);
	if(node->rb_right)
		huber_destory_socket_by_sid(huber,node->rb_right);
	huber_socket_t*hs = rb_entry(node, huber_socket_t, sid_node);
	if(hs->socket_id != -1)
		g_api->net_close(hs->socket_id);
	if(hs->reconn_timer_id != 0)
		g_api->timer_kill(hs->reconn_timer_id);
	rb_erase(&hs->peer_node,&huber->peer_map);
	g_api->free(hs);
}

static void huber_destory_socket_by_peer(huber_t*huber, struct rb_node* node){
	if(!node)
		return;
	if(node->rb_left)
		huber_destory_socket_by_peer(huber,node->rb_left);
	if(node->rb_right)
		huber_destory_socket_by_peer(huber,node->rb_right);
	huber_socket_t*hs = rb_entry(node, huber_socket_t, sid_node);
	if(hs->socket_id != -1)
		g_api->net_close(hs->socket_id);
	if(hs->reconn_timer_id != 0)
		g_api->timer_kill(hs->reconn_timer_id);
	g_api->free(hs);
}

static void huber_udfree_func_t(jetnet_entity_t*entity){
	huber_t*huber = (huber_t*)entity->ud;
	huber_destory_socket_by_sid(huber, huber->sid_map.rb_node);
	huber_destory_socket_by_peer(huber, huber->peer_map.rb_node);
	if(huber->listen_sock_id != -1){
		g_api->net_close(huber->listen_sock_id);
		huber->listen_sock_id = -1;
	}
	g_api->free(huber);
}

#if defined(__cplusplus)
extern "C" {
#endif

API_EXP int huber_init(jetnet_api_t*api);
API_EXP void huber_startup(jetnet_api_t*api);

#if defined(__cplusplus)
}
#endif

API_EXP void huber_init_2(){
	void*d = malloc(10);
	free(d);
}

API_EXP int huber_init(jetnet_api_t*api){
	//we create an entity object to hold the huber
	jetnet_entity_t*entity = api->create_entity(HUBER_ENO);
	if(!entity)
		return 0;
	g_api = api;
	huber_t* huber = (huber_t*)g_api->malloc(sizeof(huber_t));
	huber->base.type = APP_OBJ_TYPE_HUBER;
	huber->sid_map = RB_ROOT;
	huber->peer_map = RB_ROOT;
	huber->listen_sock_id = -1;
	//setup the entity info
	entity->msg_cb = &huber_msg_cb;
	entity->ud_free_func = &huber_udfree_func_t;
	entity->ud = huber;
	//we init the network
	jetnet_leid_t owner_id;
	owner_id.cno = entity->eid.cno;
	owner_id.eno = entity->eid.eno;
	huber->listen_sock_id = g_api->net_listen(owner_id,JETNET_PROTO_TCPSAC,"0.0.0.0",entity->eid.cport,102400);
	if(huber->listen_sock_id == -1){
		g_api->destroy_entity(entity);
		g_api = NULL;
		return 0;
	}
	return 1;
}

API_EXP void huber_startup(jetnet_api_t*api){
	
}
