#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include <stdio.h>

#include "jetnet_tcpsac.h"
#include "jetnet_errno.h"
#include "jetnet_time.h"
#include "jetnet_malloc.h"
#include "jetnet_msg.h"

#define TCPSAC_MAX_PACKET_LEN		1048576 //1M

//define the protocol tag
#define JETNET_TCPSAC_TAG_LEN 6
#define JETNET_TCPSAC_TAG "TCPSAC"
//define the current version
#define JETNET_TCPSAC_VERSION 0x01

//define client state
enum{
	TCPSAC_CLIENT_STATE_NONE = 0,
	TCPSAC_CLIENT_STATE_CONNECTING,
	TCPSAC_CLIENT_STATE_TRANS,
	TCPSAC_CLIENT_STATE_CLOSE,
};

//define server state
enum{
	TCPSAC_SERVER_STATE_NONE = 0,
	TCPSAC_SERVER_STATE_TRANS,
	TCPSAC_SERVER_STATE_CLOSE,
};

//define the parser step
enum{
	TCPSAC_PARSER_STEP_HEAD = 0, //wait for header data
	TCPSAC_PARSER_STEP_BODY, //wait for body data
	TCPSAC_PARSER_STEP_FINISH, //parser is finish
	TCPSAC_PARSER_STEP_ERROR, //parser encounter error
};

//define the tcpsac parser information
typedef struct jetnet_tcpsac_pi_t{
	int step; //step of the parser
	uint32_t body_len; //the body lenght get from header,valid while the step > TCPSAC_PARSER_STEP_HEAD
}jetnet_tcpsac_pi;

//define the tcpsac client protocol data
typedef struct jetnet_tcpsac_client_pd_t{
	jetnet_base_pd base;
	int state;
	jetnet_tcpsac_pi* pi;
}jetnet_tcpsac_client_pd;

//define the tcpsac server protocol data
typedef struct jetnet_tcpsac_server_pd_t{
	jetnet_base_pd base;
	int state;
	jetnet_tcpsac_pi* pi;
}jetnet_tcpsac_server_pd;

//define the tcpsac header
#pragma pack(1)
typedef struct jetnet_tcpsac_header_t{
	char protocol_tag[JETNET_TCPSAC_TAG_LEN]; //the protocol tag,for protocol recognitze
	uint32_t body_len; //the message body lenght.
	int8_t protocol_version; //the version of protocol
}jetnet_tcpsac_header;
#pragma pack()

//the globle tcpsac object
static jetnet_proto_t*jetnet_g_tcpsac = NULL;

//make an header for data send
jetnet_tcpsac_header* jetnet_tcpsac_make_header(int*header_len, uint32_t body_len){
	//caculate the lenght
	int hl = sizeof(jetnet_tcpsac_header);
	*header_len = hl;
	jetnet_tcpsac_header*header = (jetnet_tcpsac_header*)jetnet_malloc(hl);
	if(!header)
		return NULL;
	memcpy(header->protocol_tag,JETNET_TCPSAC_TAG,JETNET_TCPSAC_TAG_LEN);
	header->body_len = (uint32_t)htonl(body_len);	
	header->protocol_version = JETNET_TCPSAC_VERSION;
	return header;
}

void* jetnet_tcpsac_malloc_pi(int socket_r){
	jetnet_tcpsac_pi*pi = (jetnet_tcpsac_pi*)jetnet_malloc(sizeof(jetnet_tcpsac_pi));
	pi->step = TCPSAC_PARSER_STEP_HEAD;
	pi->body_len = 0;
	return pi;
}

void jetnet_tcpsac_free_pi(void*pi){
	jetnet_free(pi);
}

void jetnet_tcpsac_parser_reset(void*pi){
	if(pi){
		jetnet_tcpsac_pi*tcpsac_pi = (jetnet_tcpsac_pi*)pi;
		tcpsac_pi->step = TCPSAC_PARSER_STEP_HEAD;
		tcpsac_pi->body_len = 0;
	}
}

void jetnet_tcpsac_release(){
	if(jetnet_g_tcpsac){
		jetnet_free(jetnet_g_tcpsac);
		jetnet_g_tcpsac = NULL;
	}
}

void* jetnet_tcpsac_malloc_client_pd(){
	jetnet_tcpsac_client_pd*client_pd = (jetnet_tcpsac_client_pd*)jetnet_malloc(sizeof(jetnet_tcpsac_client_pd));
	client_pd->base.protocol = JETNET_PROTO_TCPSAC;
	client_pd->state = TCPSAC_CLIENT_STATE_NONE;
	client_pd->pi = (jetnet_tcpsac_pi*)jetnet_tcpsac_malloc_pi(SOCKET_R_CLIENT);
	return client_pd;
}

void jetnet_tcpsac_free_client_pd(void*pd){
	jetnet_tcpsac_client_pd*client_pd = (jetnet_tcpsac_client_pd*)pd;
	if(client_pd->pi){
		jetnet_tcpsac_free_pi(client_pd->pi);
	}
	jetnet_free(client_pd);
}

void* jetnet_tcpsac_malloc_server_pd(){
	jetnet_tcpsac_server_pd*server_pd = (jetnet_tcpsac_server_pd*)jetnet_malloc(sizeof(jetnet_tcpsac_server_pd));
	server_pd->base.protocol = JETNET_PROTO_TCPSAC;
	server_pd->state = TCPSAC_SERVER_STATE_NONE;
	server_pd->pi = (jetnet_tcpsac_pi*)jetnet_tcpsac_malloc_pi(SOCKET_R_SERVER);
	return server_pd;
}

void jetnet_tcpsac_free_server_pd(void*pd){
	jetnet_tcpsac_server_pd*server_pd = (jetnet_tcpsac_server_pd*)pd;
	if(server_pd->pi){
		jetnet_tcpsac_free_pi(server_pd->pi);
	}
	jetnet_free(server_pd);
}

/*
-1:error format
0:succeed
1:wait for more data
2:confirm and wait for more data
*/
int jetnet_tcpsac_parser(jetnet_rbl_t *rbl, jetnet_rb_t *rb, void*pi){
	jetnet_tcpsac_pi*tcpsac_pi = (jetnet_tcpsac_pi*)pi;
	switch(tcpsac_pi->step){
		case TCPSAC_PARSER_STEP_HEAD:{
			int hl = sizeof(jetnet_tcpsac_header);
			if(rbl->size < hl)
				return 1;
			jetnet_tcpsac_header header;			
			if(0 != jetnet_rbl_peek(rbl, 0, (char*)&header, hl)){
				tcpsac_pi->step = TCPSAC_PARSER_STEP_ERROR;
				return -1;
			}
			//ptotocol tag
			if(strncmp(JETNET_TCPSAC_TAG,header.protocol_tag,JETNET_TCPSAC_TAG_LEN) != 0){
				tcpsac_pi->step = TCPSAC_PARSER_STEP_ERROR;
				return -1;
			}
			//message type
			if(header.protocol_version > JETNET_TCPSAC_VERSION){
				tcpsac_pi->step = TCPSAC_PARSER_STEP_ERROR;
				return -1;
			}
			//body len
			tcpsac_pi->body_len = ntohl(header.body_len);
			if(tcpsac_pi->body_len <= 0 || tcpsac_pi->body_len > TCPSAC_MAX_PACKET_LEN){
				tcpsac_pi->step = TCPSAC_PARSER_STEP_ERROR;
				return -1;				
			}
			//read body
			if(rbl->size >= hl + tcpsac_pi->body_len){
				tcpsac_pi->step = TCPSAC_PARSER_STEP_FINISH;
				return 0;
			}else{
				tcpsac_pi->step = TCPSAC_PARSER_STEP_BODY;
				return 1;
			}
			break;
		}
		case TCPSAC_PARSER_STEP_BODY:{
			//read body
			if(rbl->size >= sizeof(jetnet_tcpsac_header) + tcpsac_pi->body_len){
				tcpsac_pi->step = TCPSAC_PARSER_STEP_FINISH;
				return 0;
			}else{
				tcpsac_pi->step = TCPSAC_PARSER_STEP_BODY;
				return 1;
			}
			break;
		}
		case TCPSAC_PARSER_STEP_FINISH:{
			return 0;
		}
		case TCPSAC_PARSER_STEP_ERROR:{
			return -1;
		}		
	}
	return -1;
}

int jetnet_tcpsac_formatter(void*data, int len, jetnet_iov_t*vec, int*vec_count){
	if(len > TCPSAC_MAX_PACKET_LEN){
		jetnet_errno = ESERIALIZE;
		return -1;
	}
	int hl = 0;
	//for default,it's a application protocol
	jetnet_tcpsac_header*header = jetnet_tcpsac_make_header(&hl, (uint32_t)len);
	vec[0].iov_base = header;
	vec[0].iov_len = (size_t)hl;
	vec[1].iov_base = data;
	vec[1].iov_len = (size_t)len;	
	*vec_count = 2;
	return 0;
}

/*
-1:fail,the buffer will be free in this function
0:succeed
*/
int jetnet_tcpsac_send_pkg(jetnet_ss_t*ss, int id, jetnet_leid_t owner_leid, uint32_t seq_id, char*buf, int len){
	int header_len = 0;
	jetnet_tcpsac_header*header = jetnet_tcpsac_make_header(&header_len, (uint32_t)len);
	if(!header){
		jetnet_free((void*)buf);
		jetnet_errno = EMEMORY;
		return -1;
	}
	jetnet_iov_t vec[2];
	vec[0].iov_base = (void*)header;
	vec[0].iov_len = header_len;
	vec[1].iov_base = (void*)buf;
	vec[1].iov_len = len;		
	if(jetnet_tcp_sendv(ss, id, owner_leid, seq_id, vec , 2) < 0)
		return -1;
	return 0;
}

/*
-1:error format
0:succeed
1:wait for more data
2:confirm and wait for more data
*/
int jetnet_tcpsac_recv_pkg(jetnet_base_socket_t*s, jetnet_rb_t*rb, jetnet_msg_t**msg){
	*msg = NULL;
	if(s->socket_r == SOCKET_R_SERVER){
		jetnet_tcpsac_server_pd*server_pd = (jetnet_tcpsac_server_pd*)s->ud;
		int ret = jetnet_tcpsac_parser(&s->rbl, rb, server_pd->pi);
		if(ret == 0){
			jetnet_tcpsac_header header;
			int read_len = (int)sizeof(header);
			if(jetnet_rbl_read(&s->rbl, (char*)&header, read_len) != read_len){
				jetnet_errno = EPARSER;
				return -1;
			}
			
			int tcppkg_data_len = server_pd->pi->body_len;			
			int cmd_len = JETNET_GET_VAR_LEN(jetnet_cmd_recv_t,tcppkg_data_len);
			int msg_len = JETNET_GET_VAR_LEN(jetnet_msg_t,cmd_len);
			jetnet_msg_t*msg_temp = (jetnet_msg_t*)jetnet_malloc(msg_len);
			memset(msg_temp,0,msg_len);
			msg_temp->dst_eid.cno = s->owner_leid.cno;
			msg_temp->dst_eid.eno = s->owner_leid.eno;
			msg_temp->msg_type = MSGT_SYS_CMD;
			MSGT_SET_CMD_TAG(msg_temp,CMDTAG_RECV);
			msg_temp->msg_len = cmd_len;
			jetnet_cmd_recv_t*sub_cmd = JETNET_GET_MSG_SUB(msg_temp,jetnet_cmd_recv_t);
			sub_cmd->socket_id = s->id;
			sub_cmd->protocol = JETNET_PROTO_TCPSAC;
			sub_cmd->len = tcppkg_data_len;
			char*recv_buf = (char*)&sub_cmd->data[0];
			if(jetnet_rbl_read(&s->rbl, recv_buf, tcppkg_data_len) != tcppkg_data_len){
				jetnet_free(msg_temp);
				jetnet_errno = EPARSER;
				return -1;
			}			
			*msg = msg_temp;			
			jetnet_tcpsac_parser_reset(server_pd->pi);
		}
		return ret;		
	}else if(s->socket_r == SOCKET_R_CLIENT){
		jetnet_tcpsac_client_pd*client_pd = (jetnet_tcpsac_client_pd*)s->ud;
		int ret = jetnet_tcpsac_parser(&s->rbl, rb, client_pd->pi);
		if(ret == 0){
			jetnet_tcpsac_header header;
			int read_len = (int)sizeof(header);
			if(jetnet_rbl_read(&s->rbl, (char*)&header, read_len) != read_len){
				jetnet_errno = EPARSER;
				return -1;
			}
			
			int tcppkg_data_len = client_pd->pi->body_len;			
			int cmd_len = JETNET_GET_VAR_LEN(jetnet_cmd_recv_t,tcppkg_data_len);
			int msg_len = JETNET_GET_VAR_LEN(jetnet_msg_t,cmd_len);
			jetnet_msg_t*msg_temp = (jetnet_msg_t*)jetnet_malloc(msg_len);
			memset(msg_temp,0,msg_len);
			msg_temp->dst_eid.cno = s->owner_leid.cno;
			msg_temp->dst_eid.eno = s->owner_leid.eno;
			msg_temp->msg_type = MSGT_SYS_CMD;
			MSGT_SET_CMD_TAG(msg_temp,CMDTAG_RECV);
			msg_temp->msg_len = cmd_len;
			jetnet_cmd_recv_t*sub_cmd = JETNET_GET_MSG_SUB(msg_temp,jetnet_cmd_recv_t);
			sub_cmd->socket_id = s->id;
			sub_cmd->protocol = JETNET_PROTO_TCPSAC;
			sub_cmd->len = tcppkg_data_len;
			char*recv_buf = (char*)&sub_cmd->data[0];
			if(jetnet_rbl_read(&s->rbl, recv_buf, tcppkg_data_len) != tcppkg_data_len){
				jetnet_free(msg_temp);
				jetnet_errno = EPARSER;
				return -1;
			}			
			*msg = msg_temp;			
			jetnet_tcpsac_parser_reset(client_pd->pi);
		}
		return ret;
	}else{
		jetnet_errno = ESOCKETROLE;
		return -1;
	}
}

void jetnet_tcpsac_poll(jetnet_ss_t*ss, jetnet_mq_t*mq){

}

void	jetnet_tcpsac_notify_unreachable(jetnet_base_socket_t*s, jetnet_mq_t*mq){
	jetnet_wb_t * cur = s->wbl.head;
	while(cur){
		jetnet_swb_t*swb = (jetnet_swb_t*)cur;
		if(JETNET_LEID_VALID(swb->owner_leid)){
			int cmd_len = sizeof(jetnet_cmd_unreach_t);
			int msg_len = JETNET_GET_VAR_LEN(jetnet_msg_t,cmd_len);
			jetnet_msg_t*msg_temp = (jetnet_msg_t*)jetnet_malloc(msg_len);
			memset(msg_temp,0,msg_len);
			msg_temp->dst_eid.cno = s->owner_leid.cno;
			msg_temp->dst_eid.eno = s->owner_leid.eno;
			msg_temp->msg_type = MSGT_SYS_CMD;
			MSGT_SET_CMD_TAG(msg_temp,CMDTAG_UNREACH);
			msg_temp->msg_len = cmd_len;
			jetnet_cmd_unreach_t*sub_cmd = JETNET_GET_MSG_SUB(msg_temp,jetnet_cmd_unreach_t);
			sub_cmd->socket_id = s->id;
			sub_cmd->protocol = JETNET_PROTO_TCPSAC;
			sub_cmd->owner_leid = swb->owner_leid;
			sub_cmd->seq_id = swb->seq_id;	
			jetnet_mq_push(mq,msg_temp);
		}
		cur = cur->next;
	}
}

void	jetnet_tcpsac_notify_close(jetnet_leid_t dst_leid, int id, int is_listener, jetnet_mq_t*mq){
	int cmd_len = sizeof(jetnet_cmd_close_t);
	int msg_len = JETNET_GET_VAR_LEN(jetnet_msg_t,cmd_len);
	jetnet_msg_t*msg_temp = (jetnet_msg_t*)jetnet_malloc(msg_len);
	memset(msg_temp,0,msg_len);
	msg_temp->dst_eid.cno = dst_leid.cno;
	msg_temp->dst_eid.eno = dst_leid.eno;
	msg_temp->msg_type = MSGT_SYS_CMD;
	MSGT_SET_CMD_TAG(msg_temp,CMDTAG_CLOSE);
	msg_temp->msg_len = cmd_len;
	jetnet_cmd_close_t*sub_cmd = JETNET_GET_MSG_SUB(msg_temp,jetnet_cmd_close_t);
	sub_cmd->socket_id = id;
	sub_cmd->protocol = JETNET_PROTO_TCPSAC;
	sub_cmd->is_listener = is_listener;
	jetnet_mq_push(mq,msg_temp);
}

void	jetnet_tcpsac_notify_connect(jetnet_leid_t dst_leid, int id, char*host, int port, int result, int errcode, jetnet_mq_t*mq){
	int cmd_len = sizeof(jetnet_cmd_connect_t);
	int msg_len = JETNET_GET_VAR_LEN(jetnet_msg_t,cmd_len);
	jetnet_msg_t*msg_temp = (jetnet_msg_t*)jetnet_malloc(msg_len);
	memset(msg_temp,0,msg_len);
	msg_temp->dst_eid.cno = dst_leid.cno;
	msg_temp->dst_eid.eno = dst_leid.eno;
	msg_temp->msg_type = MSGT_SYS_CMD;
	MSGT_SET_CMD_TAG(msg_temp,CMDTAG_CONNECT);
	msg_temp->msg_len = cmd_len;
	jetnet_cmd_connect_t*sub_cmd = JETNET_GET_MSG_SUB(msg_temp,jetnet_cmd_connect_t);
	sub_cmd->socket_id = id;
	sub_cmd->protocol = JETNET_PROTO_TCPSAC;
	sub_cmd->result = result;
	sub_cmd->errcode = errcode;
	if(host){
		int copy_len = sizeof(sub_cmd->peer_host) - 1;
		copy_len = copy_len < strlen(host) ? copy_len : strlen(host);
		memcpy(sub_cmd->peer_host,host,copy_len);
		sub_cmd->peer_host[copy_len] = 0;
	}else{
		memset(sub_cmd->peer_host,0,sizeof(sub_cmd->peer_host));
	}
	sub_cmd->peer_port = port;	
	jetnet_mq_push(mq,msg_temp);
}

void	jetnet_tcpsac_notify_accept(jetnet_leid_t dst_leid, int id, char*host, int port, jetnet_mq_t*mq){
	int cmd_len = sizeof(jetnet_cmd_accept_t);
	int msg_len = JETNET_GET_VAR_LEN(jetnet_msg_t,cmd_len);
	jetnet_msg_t*msg_temp = (jetnet_msg_t*)jetnet_malloc(msg_len);
	memset(msg_temp,0,msg_len);
	msg_temp->dst_eid.cno = dst_leid.cno;
	msg_temp->dst_eid.eno = dst_leid.eno;
	msg_temp->msg_type = MSGT_SYS_CMD;
	MSGT_SET_CMD_TAG(msg_temp,CMDTAG_ACCEPT);
	msg_temp->msg_len = cmd_len;
	jetnet_cmd_accept_t*sub_cmd = JETNET_GET_MSG_SUB(msg_temp,jetnet_cmd_accept_t);
	sub_cmd->socket_id = id;
	sub_cmd->protocol = JETNET_PROTO_TCPSAC;
	if(host){
		int copy_len = sizeof(sub_cmd->peer_host) - 1;
		copy_len = copy_len < strlen(host) ? copy_len : strlen(host);
		memcpy(sub_cmd->peer_host,host,copy_len);
		sub_cmd->peer_host[copy_len] = 0;
	}else{
		memset(sub_cmd->peer_host,0,sizeof(sub_cmd->peer_host));
	}
	sub_cmd->peer_port = port;	
	jetnet_mq_push(mq,msg_temp);
}

void	jetnet_tcpsac_handle_client_recv(jetnet_ss_t*ss, jetnet_base_socket_t*s, jetnet_rb_t*rb, jetnet_mq_t*mq){
	jetnet_tcpsac_client_pd*client_pd = (jetnet_tcpsac_client_pd*)s->ud;
	switch(client_pd->state){
		case TCPSAC_CLIENT_STATE_NONE:{
			assert(!"tcpsac client will recv data on none state!\n");
			return;
		}
		case TCPSAC_CLIENT_STATE_CONNECTING:{
			assert(!"tcpsac client will recv data on connecting state!\n");
			return;
		}
		case TCPSAC_CLIENT_STATE_TRANS:{
			jetnet_msg_t*msg = NULL;
			int ret;
			while(1){
				msg = NULL;
				ret = jetnet_tcpsac_recv_pkg(s, rb, &msg);
				//we set the rb = NULL for next call
				if(rb)
					rb = NULL;
				//check
				if(ret == 1)
					return;
				if(ret == -1){
					//notify unreachable
					jetnet_tcpsac_notify_unreachable(s, mq);					
					//notify close!
					jetnet_tcpsac_notify_close(s->owner_leid, s->id, 0, mq);						
					//set up close state
					client_pd->state = TCPSAC_CLIENT_STATE_CLOSE;
					//close the socket
					jetnet_close_socket(ss, s->id);
					return;
				}
				//we get an package
				jetnet_mq_push(mq,msg);
			}
			break;
		}
		case TCPSAC_CLIENT_STATE_CLOSE:{
			//close the socket
			jetnet_close_socket(ss, s->id);
			break;
		}
	}
}

void	jetnet_tcpsac_handle_client_close(jetnet_ss_t*ss, jetnet_base_socket_t*s, int errcode, jetnet_mq_t*mq){
	jetnet_tcpsac_client_pd*client_pd = (jetnet_tcpsac_client_pd*)s->ud;
	switch(client_pd->state){
		case TCPSAC_CLIENT_STATE_NONE:{
			assert(!"tcpsac client will not get this NONE state!\n");
			return;
		}
		case TCPSAC_CLIENT_STATE_CONNECTING:{
			//notify connect fail!
			jetnet_tcpsac_notify_connect(s->owner_leid, s->id,s->peer_host,s->peer_port,0,errcode,mq);
			//set up close state
			client_pd->state = TCPSAC_CLIENT_STATE_CLOSE;
			//close the socket
			jetnet_close_socket(ss, s->id);
			return;
		}
		case TCPSAC_CLIENT_STATE_TRANS:{
			//notify unreachable
			jetnet_tcpsac_notify_unreachable(s, mq);					
			//notify close!
			jetnet_tcpsac_notify_close(s->owner_leid, s->id, 0, mq);
			//set up close state
			client_pd->state = TCPSAC_CLIENT_STATE_CLOSE;
			//close the socket
			jetnet_close_socket(ss, s->id);
			return;
		}
		case TCPSAC_CLIENT_STATE_CLOSE:{
			//close the socket
			jetnet_close_socket(ss, s->id);
			return;
		}		
	}
}

void	jetnet_tcpsac_handle_server_recv(jetnet_ss_t*ss, jetnet_base_socket_t*s, jetnet_rb_t*rb, jetnet_mq_t*mq){
	jetnet_tcpsac_server_pd*server_pd = (jetnet_tcpsac_server_pd*)s->ud;
	switch(server_pd->state){
		case TCPSAC_SERVER_STATE_NONE:{
			assert(!"tcpsac server will not get this NONE state!\n");
			return;	
		}
		case TCPSAC_SERVER_STATE_TRANS:{
			jetnet_msg_t*msg;
			int ret;
			while(1){
				msg = NULL;
				ret = jetnet_tcpsac_recv_pkg(s, rb, &msg);
				//we set the rb = NULL for next call
				if(rb)
					rb = NULL;
				//check
				if(ret == 1)
					return;
				if(ret == -1){
					//notify unreachable
					jetnet_tcpsac_notify_unreachable(s, mq);					
					//notify close!
					jetnet_tcpsac_notify_close(s->owner_leid, s->id, 0, mq);						
					//set up close state
					server_pd->state = TCPSAC_SERVER_STATE_CLOSE;
					//close the socket
					jetnet_close_socket(ss, s->id);
					return;
				}
				//we get an package
				jetnet_mq_push(mq,msg);
			}
			break;
		}
		case TCPSAC_SERVER_STATE_CLOSE:{
			//close the socket
			jetnet_close_socket(ss, s->id);
			break;
		}
	}
}

void	jetnet_tcpsac_handle_server_close(jetnet_ss_t*ss, jetnet_base_socket_t*s, int errcode, jetnet_mq_t*mq){
	jetnet_tcpsac_server_pd*server_pd = (jetnet_tcpsac_server_pd*)s->ud;
	switch(server_pd->state){
		case TCPSAC_SERVER_STATE_NONE:{			
			assert(!"tcpsac server will not get this NONE state!\n");
			return;
		}
		case TCPSAC_SERVER_STATE_TRANS:{
			//notify unreachable
			jetnet_tcpsac_notify_unreachable(s, mq);					
			//notify close!
			jetnet_tcpsac_notify_close(s->owner_leid, s->id, 0, mq);
			//set up close state
			server_pd->state = TCPSAC_SERVER_STATE_CLOSE;
			//close the socket
			jetnet_close_socket(ss, s->id);
			return;
		}
		case TCPSAC_SERVER_STATE_CLOSE:{
			//close the socket
			jetnet_close_socket(ss, s->id);
			return;
		}		
	}
}

void jetnet_tcpsac_ev_handler(jetnet_ss_t*ss, jetnet_pe_t*ev, jetnet_mq_t*mq){
	switch(ev->ev_type){
		case POLL_EV_CONNECT:{
			jetnet_base_socket_t*s = ev->s;
			jetnet_tcpsac_client_pd*client_pd = (jetnet_tcpsac_client_pd*)s->ud;
			if(ev->data.connect.result == 0){//connect fail.
				//notify connect fail
				jetnet_tcpsac_notify_connect(s->owner_leid, s->id,s->peer_host,s->peer_port,0,ev->data.connect.errcode,mq);
				//set up close state
				client_pd->state = TCPSAC_CLIENT_STATE_CLOSE;
				//close the socket
				jetnet_close_socket(ss, s->id);
			}else{//connect succeed
				//we init the parser information
				jetnet_tcpsac_parser_reset(client_pd->pi);
				client_pd->state = TCPSAC_CLIENT_STATE_TRANS;
				jetnet_tcpsac_notify_connect(s->owner_leid, s->id,s->peer_host,s->peer_port,1,0,mq);
			}
			break;
		}
		case POLL_EV_ACCEPT:{
			jetnet_base_socket_t*s = ev->s;
			jetnet_tcpsac_server_pd*server_pd = (jetnet_tcpsac_server_pd*)jetnet_tcpsac_malloc_server_pd();
			server_pd->state = TCPSAC_SERVER_STATE_TRANS;
			jetnet_tcpsac_parser_reset(server_pd->pi);
			s->ud = server_pd;
			s->free_func = jetnet_tcpsac_free_server_pd;
			jetnet_tcpsac_notify_accept(s->owner_leid, s->id,s->peer_host,s->peer_port,mq);
			break;
		}		
		case POLL_EV_RECV:{
			jetnet_base_socket_t*s = ev->s;
			if(SOCKET_R_CLIENT == s->socket_r){
				jetnet_tcpsac_handle_client_recv(ss, s, ev->data.recv.rb, mq);
			}else if(SOCKET_R_SERVER == s->socket_r){
				jetnet_tcpsac_handle_server_recv(ss, s, ev->data.recv.rb, mq);
			}else{
				assert(!"can't recv data in this socket role!\n");
			}
			break;
		}
		case POLL_EV_CLOSE:{
			jetnet_base_socket_t*s = ev->s;
			if(SOCKET_R_CLIENT == s->socket_r){
				jetnet_tcpsac_handle_client_close(ss, s, ev->data.close.errcode, mq);
			}else if(SOCKET_R_SERVER == s->socket_r){
				jetnet_tcpsac_handle_server_close(ss, s, ev->data.close.errcode, mq);
			}else if(SOCKET_R_LISTENER == s->socket_r){
				//the listener is close.
				jetnet_tcpsac_notify_close(s->owner_leid, s->id, 1, mq);
				//close
				jetnet_close_socket(ss, s->id);
			}
			break;
		}
	}
}

int jetnet_tcpsac_listen(jetnet_ss_t*ss, jetnet_leid_t owner_leid, const char * host, int port, int backlog){
	int ret = jetnet_tcp_listen(ss, owner_leid, host, port, backlog);
	if(ret >= 0){
		jetnet_base_socket_t*s = jetnet_get_socket(ss, ret);
		jetnet_base_pd*listenser_pd = (jetnet_base_pd*)jetnet_malloc(sizeof(jetnet_base_pd));
		listenser_pd->protocol = JETNET_PROTO_TCPSAC;		
		s->ud = listenser_pd;
		s->free_func = NULL;
	}
	return ret;
}

int jetnet_tcpsac_connect(jetnet_ss_t*ss, jetnet_leid_t owner_leid, const char * host, int port){
	int ret = jetnet_tcp_connect(ss, owner_leid, host, port);
	if(ret >= 0){
		jetnet_base_socket_t*s = jetnet_get_socket(ss, ret);
		jetnet_tcpsac_client_pd*client_pd = (jetnet_tcpsac_client_pd*)jetnet_tcpsac_malloc_client_pd();
		client_pd->pi = (jetnet_tcpsac_pi*)jetnet_malloc(sizeof(jetnet_tcpsac_pi));
		client_pd->base.protocol = JETNET_PROTO_TCPSAC;
		client_pd->state = TCPSAC_CLIENT_STATE_CONNECTING;
		//reset the pi
		jetnet_tcpsac_parser_reset(client_pd->pi);			
		s->ud = client_pd;
		s->free_func = &jetnet_tcpsac_free_client_pd;			
		//
		if(jetnet_tcp_is_connected(ss, ret)){
			//now we should send an valid_req package
			client_pd->state = TCPSAC_CLIENT_STATE_TRANS;
		}
	}
	return ret;
}

int jetnet_tcpsac_send(jetnet_ss_t*ss, int id, jetnet_leid_t owner_leid, uint32_t seq_id, void* data, int len){
	jetnet_base_socket_t*s = jetnet_get_socket(ss, id);
	if(!s){
		jetnet_errno = ELOCATESOCKET;
		jetnet_free(data);
		return -1;
	}
	if(s->socket_r == SOCKET_R_CLIENT){
		jetnet_tcpsac_client_pd*client_pd = (jetnet_tcpsac_client_pd*)s->ud;
		if(!client_pd){
			jetnet_errno = ESOCKETSTATE;
			jetnet_free(data);
			return -1;
		}
		switch(client_pd->state){
			case TCPSAC_CLIENT_STATE_NONE:
			case TCPSAC_CLIENT_STATE_CONNECTING:
			case TCPSAC_CLIENT_STATE_CLOSE:{
				jetnet_errno = ESOCKETSTATE;
				jetnet_free(data);
				return -1;
			}
			case TCPSAC_CLIENT_STATE_TRANS:{
				jetnet_iov_t vec[JETNET_MAX_IOVEC];
				int vec_count = 0;			
				if(jetnet_tcpsac_formatter(data, len, vec,&vec_count) < 0){
					jetnet_free(data);
					jetnet_errno = ESERIALIZE;
					return -1;
				}
				return jetnet_tcp_sendv(ss, id, owner_leid, seq_id, vec ,vec_count);
			}
		}
	}else if(s->socket_r == SOCKET_R_SERVER){
		jetnet_tcpsac_server_pd*server_pd = (jetnet_tcpsac_server_pd*)s->ud;
		if(!server_pd){
			jetnet_errno = ESOCKETSTATE;
			jetnet_free(data);
			return -1;
		}
		switch(server_pd->state){
			case TCPSAC_SERVER_STATE_NONE:
			case TCPSAC_SERVER_STATE_CLOSE:{
				jetnet_errno = ESOCKETSTATE;
				jetnet_free(data);
				return -1;
			}
			case TCPSAC_SERVER_STATE_TRANS:{
				jetnet_iov_t vec[JETNET_MAX_IOVEC];
				int vec_count = 0;			
				if(jetnet_tcpsac_formatter(data, len, vec,&vec_count) < 0){
					jetnet_free(data);
					jetnet_errno = ESERIALIZE;
					return -1;
				}
				return jetnet_tcp_sendv(ss, id, owner_leid, seq_id, vec ,vec_count);
			}
		}
	}else{
		jetnet_errno = ESOCKETROLE;
		jetnet_free(data);
		return -1;
	}

	jetnet_free(data);
	return -1;
}

void jetnet_tcpsac_close(jetnet_ss_t*ss, int id){
	jetnet_base_socket_t*s = jetnet_get_socket(ss, id);
	if(!s)
		return;
	if(s->socket_r == SOCKET_R_CLIENT){
		jetnet_tcpsac_client_pd*client_pd = (jetnet_tcpsac_client_pd*)s->ud;
		client_pd->state = TCPSAC_CLIENT_STATE_CLOSE;
	}else if(s->socket_r == SOCKET_R_SERVER){
		jetnet_tcpsac_server_pd*server_pd = (jetnet_tcpsac_server_pd*)s->ud;
		server_pd->state = TCPSAC_SERVER_STATE_CLOSE;
	}
	jetnet_close_socket(ss, id);
}

jetnet_proto_t* jetnet_tcpsac_create(){
	if(jetnet_g_tcpsac)
		return jetnet_g_tcpsac;	
	jetnet_g_tcpsac = (jetnet_proto_t*)jetnet_malloc(sizeof(jetnet_proto_t));
	jetnet_g_tcpsac->f_malloc_pi = &jetnet_tcpsac_malloc_pi;
	jetnet_g_tcpsac->free_pi = &jetnet_tcpsac_free_pi;
	jetnet_g_tcpsac->parser_reset = &jetnet_tcpsac_parser_reset;
	jetnet_g_tcpsac->parser = &jetnet_tcpsac_parser;
	jetnet_g_tcpsac->poll = &jetnet_tcpsac_poll;
	jetnet_g_tcpsac->ev_handler = &jetnet_tcpsac_ev_handler;
	jetnet_g_tcpsac->listen = &jetnet_tcpsac_listen;	
	jetnet_g_tcpsac->connect = &jetnet_tcpsac_connect;
	jetnet_g_tcpsac->send = &jetnet_tcpsac_send;
	jetnet_g_tcpsac->close = &jetnet_tcpsac_close;	
	return jetnet_g_tcpsac;
}