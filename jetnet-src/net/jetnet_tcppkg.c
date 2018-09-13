#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include <stdio.h>

#include "jetnet_tcppkg.h"
#include "jetnet_errno.h"
#include "jetnet_time.h"
#include "jetnet_malloc.h"
#include "list.h"
#include "jetnet_msg.h"
#include "jetnet_macro.h"

//define the protocol tag
#define JETNET_TCPPKG_TAG 0xA12C9085
//define the current version
#define JETNET_TCPPKG_VERSION 0x01
//define the max package for this protocol
#define TCPPKG_MAX_PACKET_LEN 1048576
//the max time for client to valid
#define MAX_TCPPKG_VALID_TIME 5000

//define the message type
enum{
	TCPPKG_MESSAGE_TYPE_APP = 0,
	TCPPKG_MESSAGE_TYPE_SYS,
};

//define client state
enum{
	TCPPKG_CLIENT_STATE_NONE = 0,
	TCPPKG_CLIENT_STATE_CONNECTING, //client is connecting
	TCPPKG_CLIENT_STATE_VALIDING, // validing,just for client
	TCPPKG_CLIENT_STATE_TRANS, // had recv an valid packet
	TCPPKG_CLIENT_STATE_CLOSE,
};

//define server state
enum{
	TCPPKG_SERVER_STATE_NONE = 0,
	TCPPKG_SERVER_STATE_CONNECTED, //client is connecting
	TCPPKG_SERVER_STATE_TRANS, // had recv an valid packet
	TCPPKG_SERVER_STATE_CLOSE,
};

//define the parser step
enum{
	TCPPKG_PARSER_STEP_HEAD = 0, //wait for header data
	TCPPKG_PARSER_STEP_BODY, //wait for body data
	TCPPKG_PARSER_STEP_FINISH, //parser is finish
	TCPPKG_PARSER_STEP_ERROR, //parser encounter error
};

//define the tcppkg parser information
typedef struct jetnet_tcppkg_pi_t{
	int step; //step of the parser
	uint32_t body_len; //the body lenght get from header,valid while the step > TCPPKG_PARSER_STEP_HEAD
}jetnet_tcppkg_pi;

//define the tcppkg client protocol data
typedef struct jetnet_tcppkg_client_pd_t{
	jetnet_base_pd base;
	int state;
	jetnet_tcppkg_pi* pi;	
}jetnet_tcppkg_client_pd;

//define the tcppkg server protocol data
typedef struct jetnet_tcppkg_server_pd_t{
	jetnet_base_pd base;
	int state;
	struct list_head link_node;
	jetnet_tcppkg_pi* pi;	
}jetnet_tcppkg_server_pd;

//define the tcppkg protocol object
typedef struct jetnet_net_tcppkg_t{
	jetnet_proto_t base;
	struct list_head link_list;
}jetnet_net_tcppkg;

//define the tcppkg header
#pragma pack(1)
typedef struct jetnet_tcppkg_header_t{
	int32_t random; //an random mask number
	uint16_t head_checksum;	//the checksum of the header,we use it to increase the difficulty of cracking
	int8_t protocol_version; //the version of protocol
	uint32_t body_len; //the message body lenght.
	int32_t protocol_tag; //the protocol tag,for protocol recognitze
	uint8_t message_type; //define the message type.0:application message 1:system message
}jetnet_tcppkg_header;
#pragma pack()

//define the valid req&ack protocol
#define TCPPKG_CMD_UNKNOW	  0x00000000
#define TCPPKG_CMD_VALID_REQ  0x00000001
#define TCPPKG_CMD_VALID_ACK  0x00000002

#pragma pack(1)
typedef struct jetnet_tcppkg_valid_req_t{
	int32_t		key;	
}jetnet_tcppkg_valid_req;

typedef struct jetnet_tcppkg_valid_ack_t{
	int32_t		key;	
}jetnet_tcppkg_valid_ack;


typedef struct jetnet_tcppkg_cmd_t{
	int32_t	cmd_id;
	union{
		jetnet_tcppkg_valid_req valid_req;
		jetnet_tcppkg_valid_ack valid_ack;
	}data;
}jetnet_tcppkg_cmd;
#pragma pack()

//the globle tcppkg object
static jetnet_net_tcppkg*jetnet_g_tcppkg = NULL;


uint16_t jetnet_tcppkg_chksum(void *dataptr, uint16_t len)
{
	uint32_t acc;
	uint16_t src;
	uint8_t *octetptr;
	acc = 0;
	octetptr = (uint8_t*)dataptr;
	while (len > 1) {
		src = (*octetptr) << 8;
		octetptr++;
		src |= (*octetptr);
		octetptr++;
		acc += src;
		len -= 2;
	}
	if (len > 0) {
		src = (*octetptr) << 8;
		acc += src;
	}
	acc = (acc >> 16) + (acc & 0x0000ffffUL);
	if ((acc & 0xffff0000UL) != 0) {
		acc = (acc >> 16) + (acc & 0x0000ffffUL);
	}
	src = (uint16_t)acc;
	return ~src;
}

//make an header for data send
jetnet_tcppkg_header* jetnet_tcppkg_make_header(int*header_len, uint32_t body_len, uint8_t message_type){
	//caculate the lenght
	int hl = sizeof(jetnet_tcppkg_header);
	*header_len = hl;
	jetnet_tcppkg_header*header = (jetnet_tcppkg_header*)jetnet_malloc(hl);
	if(!header)
		return NULL;
	header->random = htonl(rand());
	header->head_checksum = 0;
	header->protocol_version = JETNET_TCPPKG_VERSION;
	header->body_len = (uint32_t)htonl(body_len);
	header->protocol_tag = htonl(JETNET_TCPPKG_TAG);
	header->message_type = message_type;
	header->head_checksum = (uint16_t)htons(jetnet_tcppkg_chksum(header, (uint16_t)hl));
	return header;
}

void* jetnet_tcppkg_malloc_pi(int socket_r){
	jetnet_tcppkg_pi*pi = (jetnet_tcppkg_pi*)jetnet_malloc(sizeof(jetnet_tcppkg_pi));
	pi->step = TCPPKG_PARSER_STEP_HEAD;
	pi->body_len = 0;
	return pi;
}

inline void jetnet_tcppkg_free_pi(void*pi){
	jetnet_free(pi);
}

void jetnet_tcppkg_parser_reset(void*pi){
	if(pi){
		jetnet_tcppkg_pi*tcppkg_pi = (jetnet_tcppkg_pi*)pi;
		tcppkg_pi->step = TCPPKG_PARSER_STEP_HEAD;
		tcppkg_pi->body_len = 0;
	}
}

void jetnet_tcppkg_release(){
	if(jetnet_g_tcppkg){
		//clean the link
		while(!list_empty(&jetnet_g_tcppkg->link_list))
			list_del_init(jetnet_g_tcppkg->link_list.next);
		INIT_LIST_HEAD(&jetnet_g_tcppkg->link_list);
		//free resource
		jetnet_free(jetnet_g_tcppkg);
		jetnet_g_tcppkg = NULL;
	}
}

void* jetnet_tcppkg_malloc_client_pd(){
	jetnet_tcppkg_client_pd*client_pd = (jetnet_tcppkg_client_pd*)jetnet_malloc(sizeof(jetnet_tcppkg_client_pd));
	client_pd->base.protocol = JETNET_PROTO_TCPPKG;
	client_pd->state = TCPPKG_CLIENT_STATE_NONE;	
	client_pd->pi = (jetnet_tcppkg_pi*)jetnet_tcppkg_malloc_pi(SOCKET_R_CLIENT);
	return client_pd;
}

void jetnet_tcppkg_free_client_pd(void*pd){
	jetnet_tcppkg_client_pd*client_pd = (jetnet_tcppkg_client_pd*)pd;
	if(client_pd->pi){
		jetnet_tcppkg_free_pi(client_pd->pi);
	}
	jetnet_free(client_pd);
}

void* jetnet_tcppkg_malloc_server_pd(){
	jetnet_tcppkg_server_pd*server_pd = (jetnet_tcppkg_server_pd*)jetnet_malloc(sizeof(jetnet_tcppkg_server_pd));
	server_pd->base.protocol = JETNET_PROTO_TCPPKG;
	server_pd->state = TCPPKG_SERVER_STATE_NONE;
	LIST_NODE_INIT(&server_pd->link_node);	
	server_pd->pi = (jetnet_tcppkg_pi*)jetnet_tcppkg_malloc_pi(SOCKET_R_SERVER);
	return server_pd;
}

void jetnet_tcppkg_free_server_pd(void*pd){
	jetnet_tcppkg_server_pd*server_pd = (jetnet_tcppkg_server_pd*)pd;
	if(server_pd->pi){
		jetnet_tcppkg_free_pi(server_pd->pi);
	}
	jetnet_free(server_pd);
}

/*
-1:error format
0:succeed
1:wait for more data
2:confirm and wait for more data
*/
int jetnet_tcppkg_parser(jetnet_rbl_t *rbl, jetnet_rb_t *rb, void*pi){
	jetnet_tcppkg_pi*tcppkg_pi = (jetnet_tcppkg_pi*)pi;
	switch(tcppkg_pi->step){
		case TCPPKG_PARSER_STEP_HEAD:{
			int hl = sizeof(jetnet_tcppkg_header);
			if(rbl->size < hl)
				return 1;
			jetnet_tcppkg_header header;			
			if(0 != jetnet_rbl_peek(rbl, 0, (char*)&header, hl)){
				tcppkg_pi->step = TCPPKG_PARSER_STEP_ERROR;
				return -1;
			}
			//checksum
			uint16_t checksum = header.head_checksum;
			header.head_checksum = 0;
			uint16_t get_checksum = (uint16_t)htons(jetnet_tcppkg_chksum(&header, (uint16_t)hl));
			if(checksum != get_checksum){
				tcppkg_pi->step = TCPPKG_PARSER_STEP_ERROR;
				return -1;
			}else{
				header.head_checksum = checksum;
			}
			//ptotocol tag
			if(JETNET_TCPPKG_TAG != ntohl(header.protocol_tag)){
				tcppkg_pi->step = TCPPKG_PARSER_STEP_ERROR;
				return -1;
			}
			//message type
			if(0 != header.message_type && 1 != header.message_type){
				tcppkg_pi->step = TCPPKG_PARSER_STEP_ERROR;
				return -1;
			}
			//message type
			if(header.protocol_version > JETNET_TCPPKG_VERSION){
				tcppkg_pi->step = TCPPKG_PARSER_STEP_ERROR;
				return -1;
			}
			//body len
			tcppkg_pi->body_len = ntohl(header.body_len);
			if(tcppkg_pi->body_len <= 0 || tcppkg_pi->body_len > TCPPKG_MAX_PACKET_LEN){
				tcppkg_pi->step = TCPPKG_PARSER_STEP_ERROR;
				return -1;				
			}
			//read body
			if(rbl->size >= hl + tcppkg_pi->body_len){
				tcppkg_pi->step = TCPPKG_PARSER_STEP_FINISH;
				return 0;
			}else{
				tcppkg_pi->step = TCPPKG_PARSER_STEP_BODY;
				return 1;
			}
			break;
		}
		case TCPPKG_PARSER_STEP_BODY:{
			//read body
			if(rbl->size >= sizeof(jetnet_tcppkg_header) + tcppkg_pi->body_len){
				tcppkg_pi->step = TCPPKG_PARSER_STEP_FINISH;
				return 0;
			}else{
				tcppkg_pi->step = TCPPKG_PARSER_STEP_BODY;
				return 1;
			}
			break;
		}
		case TCPPKG_PARSER_STEP_FINISH:{
			return 0;
		}
		case TCPPKG_PARSER_STEP_ERROR:{
			return -1;
		}		
	}
	return -1;
}

int jetnet_tcppkg_formatter(void*data, int len, jetnet_iov_t*vec, int*vec_count){
	if(len > TCPPKG_MAX_PACKET_LEN){
		jetnet_errno = ESERIALIZE;
		return -1;
	}
	int hl = 0;
	//for default,it's a application protocol
	jetnet_tcppkg_header*header = jetnet_tcppkg_make_header(&hl, (uint32_t)len, TCPPKG_MESSAGE_TYPE_APP);
	vec[0].iov_base = header;
	vec[0].iov_len = (size_t)hl;
	vec[1].iov_base = data;
	vec[1].iov_len = (size_t)len;	
	*vec_count = 2;
	return 0;
}

//cmd helper
int jetnet_tcppkg_pack_cmd(jetnet_tcppkg_cmd*cmd,char**buf , int*len){
	switch(cmd->cmd_id){
		case TCPPKG_CMD_VALID_REQ:{
			int total_len = 4 + sizeof(jetnet_tcppkg_valid_req);
			*len = total_len;				
			*buf = (char*)jetnet_malloc(total_len);
			jetnet_tcppkg_cmd*valid_req = (jetnet_tcppkg_cmd*)(*buf);
			valid_req->cmd_id = htonl(cmd->cmd_id);
			valid_req->data.valid_req.key = htonl(cmd->data.valid_req.key);
			break;
		}
		case TCPPKG_CMD_VALID_ACK:{
			int total_len = 4 + sizeof(jetnet_tcppkg_valid_ack);
			*len = total_len;				
			*buf = (char*)jetnet_malloc(total_len);
			jetnet_tcppkg_cmd*valid_ack = (jetnet_tcppkg_cmd*)(*buf);
			valid_ack->cmd_id = htonl(cmd->cmd_id);
			valid_ack->data.valid_ack.key = htonl(cmd->data.valid_ack.key);
			break;
		}
		default:{
			jetnet_errno = ESERIALIZE;
			*buf = NULL;
			*len = 0;
			return -1;
		}
	}
	return 0;
}

/*
-1:fail,the buffer will be free in this function
0:succeed
*/
int jetnet_tcppkg_send_pkg(jetnet_ss_t*ss, int id, jetnet_leid_t owner_leid, uint32_t seq_id, char*buf, int len, uint8_t mssage_type){
	int header_len = 0;
	jetnet_tcppkg_header*header = jetnet_tcppkg_make_header(&header_len, (uint32_t)len, mssage_type);
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
-1:fail
0:succeed
*/
int jetnet_tcppkg_unpack_cmd(char*buf, int len, jetnet_tcppkg_cmd*cmd){
	cmd->cmd_id = TCPPKG_CMD_UNKNOW;
	if(!buf || len == 0){
		jetnet_errno = ESERIALIZE;
		return -1;
	}
	if(len < 4){
		jetnet_errno = ESERIALIZE;
		return -1;
	}
	cmd->cmd_id = ntohl(*((int32_t*)buf));
	buf += 4;
	len -= 4;
	switch(cmd->cmd_id){
		case TCPPKG_CMD_VALID_REQ:{
			if(len < sizeof(jetnet_tcppkg_valid_req)){
				jetnet_errno = ESERIALIZE;
				return -1;
			}
			cmd->data.valid_req.key = ntohl(*((int32_t*)buf));
			return 0;
		}
		case TCPPKG_CMD_VALID_ACK:{
			if(len < sizeof(jetnet_tcppkg_valid_ack)){
				jetnet_errno = ESERIALIZE;
				return -1;
			}
			cmd->data.valid_ack.key = ntohl(*((int32_t*)buf));
			return 0;
		}
	}
	jetnet_errno = ESERIALIZE;
	return -1;
}

/*
-1:error format
0:succeed
1:wait for more data
2:confirm and wait for more data
*/
int jetnet_tcppkg_recv_pkg(jetnet_base_socket_t*s, jetnet_rb_t*rb, jetnet_msg_t**msg, uint8_t message_type){
	*msg = NULL;
	if(s->socket_r == SOCKET_R_SERVER){
		jetnet_tcppkg_server_pd*server_pd = (jetnet_tcppkg_server_pd*)s->ud;
		int ret = jetnet_tcppkg_parser(&s->rbl, rb, server_pd->pi);
		if(ret == 0){
			jetnet_tcppkg_header header;
			int read_len = (int)sizeof(header);
			if(jetnet_rbl_read(&s->rbl, (char*)&header, read_len) != read_len){
				jetnet_errno = EPARSER;
				return -1;
			}
			//check the message type
			if(header.message_type != message_type){
				server_pd->pi->step = TCPPKG_PARSER_STEP_ERROR;
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
			sub_cmd->protocol = JETNET_PROTO_TCPPKG;
			sub_cmd->len = tcppkg_data_len;
			char*recv_buf = (char*)&sub_cmd->data[0];
			if(jetnet_rbl_read(&s->rbl, recv_buf, tcppkg_data_len) != tcppkg_data_len){
				jetnet_free(msg_temp);
				jetnet_errno = EPARSER;
				return -1;
			}			
			*msg = msg_temp;
			
			jetnet_tcppkg_parser_reset(server_pd->pi);
		}
		return ret;		
	}else if(s->socket_r == SOCKET_R_CLIENT){
		jetnet_tcppkg_client_pd*client_pd = (jetnet_tcppkg_client_pd*)s->ud;
		int ret = jetnet_tcppkg_parser(&s->rbl, rb, client_pd->pi);
		if(ret == 0){
			jetnet_tcppkg_header header;
			int read_len = (int)sizeof(header);
			if(jetnet_rbl_read(&s->rbl, (char*)&header, read_len) != read_len){
				jetnet_errno = EPARSER;
				return -1;
			}
			//check the message type
			if(header.message_type != message_type){
				client_pd->pi->step = TCPPKG_PARSER_STEP_ERROR;
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
			sub_cmd->protocol = JETNET_PROTO_TCPPKG;
			sub_cmd->len = tcppkg_data_len;
			char*recv_buf = (char*)&sub_cmd->data[0];
			if(jetnet_rbl_read(&s->rbl, recv_buf, tcppkg_data_len) != tcppkg_data_len){
				jetnet_free(msg_temp);
				jetnet_errno = EPARSER;
				return -1;
			}			
			*msg = msg_temp;
			
			jetnet_tcppkg_parser_reset(client_pd->pi);
		}
		return ret;
	}else{
		jetnet_errno = ESOCKETROLE;
		return -1;
	}
}

void jetnet_tcppkg_poll(jetnet_ss_t*ss, jetnet_mq_t*mq){
	//we should check the unvalid socket list
	if(!jetnet_g_tcppkg)
		return;
	while(!list_empty(&jetnet_g_tcppkg->link_list)){
		jetnet_tcppkg_server_pd*server_pd = list_entry(jetnet_g_tcppkg->link_list.next, jetnet_tcppkg_server_pd, link_node);
		jetnet_base_socket_t*s = container_of(((void*)server_pd),jetnet_base_socket_t,ud);
		if(s->socket_r == SOCKET_R_SERVER){
			if( jetnet_time_now() - s->establish_time < MAX_TCPPKG_VALID_TIME)
				break;
			//remove from links
			list_del(&server_pd->link_node);
			//setup state
			server_pd->state = TCPPKG_SERVER_STATE_CLOSE;
			//close socket
			jetnet_close_socket(ss,s->id);
		}else{
			//remove from links
			list_del(&server_pd->link_node);
		}
	}
}

void	jetnet_tcppkg_notify_unreachable(jetnet_base_socket_t*s, jetnet_mq_t*mq){
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
			sub_cmd->protocol = JETNET_PROTO_TCPPKG;
			sub_cmd->owner_leid = swb->owner_leid;
			sub_cmd->seq_id = swb->seq_id;	
			jetnet_mq_push(mq,msg_temp);
		}
		cur = cur->next;
	}
}

void	jetnet_tcppkg_notify_close(jetnet_leid_t dst_leid, int id, int is_listener, jetnet_mq_t*mq){
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
	sub_cmd->protocol = JETNET_PROTO_TCPPKG;
	sub_cmd->is_listener = is_listener;
	jetnet_mq_push(mq,msg_temp);
}

void	jetnet_tcppkg_notify_connect(jetnet_leid_t dst_leid, int id, char*host , int port , int result , int errcode , jetnet_mq_t*mq){
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
	sub_cmd->protocol = JETNET_PROTO_TCPPKG;
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

void	jetnet_tcppkg_notify_accept(jetnet_leid_t dst_leid, int id, char*host , int port , jetnet_mq_t*mq){
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
	sub_cmd->protocol = JETNET_PROTO_TCPPKG;
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

void	jetnet_tcppkg_handle_client_recv(jetnet_ss_t*ss, jetnet_base_socket_t*s,jetnet_rb_t*rb,jetnet_mq_t*mq){
	jetnet_tcppkg_client_pd*client_pd = (jetnet_tcppkg_client_pd*)s->ud;
	switch(client_pd->state){
		case TCPPKG_CLIENT_STATE_NONE:{
			assert(!"tcppkg client will recv data on none state!\n");
			return;
		}
		case TCPPKG_CLIENT_STATE_CONNECTING:{
			assert(!"tcppkg client will recv data on connecting state!\n");
			return;
		}
		case TCPPKG_CLIENT_STATE_VALIDING:{
			jetnet_msg_t*msg = NULL;
			int ret = jetnet_tcppkg_recv_pkg(s, rb, &msg,TCPPKG_MESSAGE_TYPE_SYS);
			if(ret == 1)
				return;
			if(ret == -1){
				//notify connect fail!
				jetnet_tcppkg_notify_connect(s->owner_leid, s->id,s->peer_host,s->peer_port,0,jetnet_errno,mq);	
				//set up close state
				client_pd->state = TCPPKG_CLIENT_STATE_CLOSE;
				//close the socket
				jetnet_close_socket(ss, s->id);
				return;
			}
			jetnet_tcppkg_cmd pkg_cmd;
			jetnet_cmd_recv_t*cmd_recv = JETNET_GET_MSG_SUB(msg,jetnet_cmd_recv_t);
			char*recv_buf = (char*)&cmd_recv->data[0];
			int recv_len = cmd_recv->len;
			ret = jetnet_tcppkg_unpack_cmd(recv_buf,recv_len,&pkg_cmd);
			//free the msg first.
			jetnet_free(msg);
			//check result
			if(ret < 0){
				//notify connect fail!
				jetnet_tcppkg_notify_connect(s->owner_leid, s->id,s->peer_host,s->peer_port,0,jetnet_errno,mq);
				//set up close state
				client_pd->state = TCPPKG_CLIENT_STATE_CLOSE;
				//close the socket
				jetnet_close_socket(ss, s->id);
				return;
			}
			if(pkg_cmd.cmd_id != TCPPKG_CMD_VALID_ACK){
				//notify connect fail!
				jetnet_tcppkg_notify_connect(s->owner_leid, s->id,s->peer_host,s->peer_port,0,EPROTOCOL,mq);
				//set up close state
				client_pd->state = TCPPKG_CLIENT_STATE_CLOSE;
				//close the socket
				jetnet_close_socket(ss, s->id);
				return;
			}
			//check the ack data... ...
			
			//ok,we get an valid ack
			client_pd->state = TCPPKG_CLIENT_STATE_TRANS;
			//now we should notify the connection has been established
			jetnet_tcppkg_notify_connect(s->owner_leid, s->id,s->peer_host,s->peer_port,1,0,mq);
			//and then,we should recv package recurrent
			while(1){
				msg = NULL;
				ret = jetnet_tcppkg_recv_pkg(s, NULL, &msg, TCPPKG_MESSAGE_TYPE_APP);
				if(ret == 1)
					return;
				if(ret == -1){				
					//notify close!
					jetnet_tcppkg_notify_close(s->owner_leid, s->id, 0, mq);						
					//set up close state
					client_pd->state = TCPPKG_CLIENT_STATE_CLOSE;
					//close the socket
					jetnet_close_socket(ss, s->id);
					return;
				}
				//we get an package
				jetnet_mq_push(mq,msg);
			}
			break;
		}
		case TCPPKG_CLIENT_STATE_TRANS:{
			jetnet_msg_t*msg;
			int ret;
			while(1){
				msg = NULL;
				ret = jetnet_tcppkg_recv_pkg(s, rb, &msg,TCPPKG_MESSAGE_TYPE_APP);
				//we set the rb = NULL for next call
				if(rb)
					rb = NULL;
				//check
				if(ret == 1)
					return;
				if(ret == -1){
					//notify unreachable
					jetnet_tcppkg_notify_unreachable(s, mq);					
					//notify close!
					jetnet_tcppkg_notify_close(s->owner_leid, s->id, 0, mq);						
					//set up close state
					client_pd->state = TCPPKG_CLIENT_STATE_CLOSE;
					//close the socket
					jetnet_close_socket(ss, s->id);
					return;
				}
				//we get an package
				jetnet_mq_push(mq,msg);
			}
			break;
		}
		case TCPPKG_CLIENT_STATE_CLOSE:{
			//close the socket
			jetnet_close_socket(ss, s->id);
			break;
		}
	}
}

void	jetnet_tcppkg_handle_client_close(jetnet_ss_t*ss, jetnet_base_socket_t*s, int errcode, jetnet_mq_t*mq){
	jetnet_tcppkg_client_pd*client_pd = (jetnet_tcppkg_client_pd*)s->ud;
	switch(client_pd->state){
		case TCPPKG_CLIENT_STATE_NONE:{
			assert(!"tcppkg client will not get this NONE state!\n");
			return;
		}
		case TCPPKG_CLIENT_STATE_CONNECTING:{
			//notify connect fail!
			jetnet_tcppkg_notify_connect(s->owner_leid, s->id,s->peer_host,s->peer_port,0,errcode,mq);
			//set up close state
			client_pd->state = TCPPKG_CLIENT_STATE_CLOSE;
			//close the socket
			jetnet_close_socket(ss, s->id);
			return;
		}
		case TCPPKG_CLIENT_STATE_VALIDING:{
			//notify connect fail!
			jetnet_tcppkg_notify_connect(s->owner_leid, s->id,s->peer_host,s->peer_port,0,errcode,mq);
			//set up close state
			client_pd->state = TCPPKG_CLIENT_STATE_CLOSE;
			//close the socket
			jetnet_close_socket(ss, s->id);
			return;
		}
		case TCPPKG_CLIENT_STATE_TRANS:{
			//notify unreachable
			jetnet_tcppkg_notify_unreachable(s, mq);					
			//notify close!
			jetnet_tcppkg_notify_close(s->owner_leid, s->id, 0, mq);						
			//set up close state
			client_pd->state = TCPPKG_CLIENT_STATE_CLOSE;
			//close the socket
			jetnet_close_socket(ss, s->id);
			return;
		}
		case TCPPKG_CLIENT_STATE_CLOSE:{
			//close the socket
			jetnet_close_socket(ss, s->id);
			return;
		}		
	}
}

void	jetnet_tcppkg_handle_server_recv(jetnet_ss_t*ss, jetnet_base_socket_t*s,jetnet_rb_t*rb,jetnet_mq_t*mq){
	jetnet_tcppkg_server_pd*server_pd = (jetnet_tcppkg_server_pd*)s->ud;
	switch(server_pd->state){
		case TCPPKG_SERVER_STATE_NONE:{
			assert(!"tcppkg server will not get this NONE state!\n");
			return;	
		}		
		case TCPPKG_SERVER_STATE_CONNECTED:{
			jetnet_msg_t*msg = NULL;
			int ret = jetnet_tcppkg_recv_pkg(s, rb, &msg,TCPPKG_MESSAGE_TYPE_SYS);
			if(ret == 1)
				return;
			if(ret == -1){
				//we remove it from links
				list_del(&server_pd->link_node);
				//set up close state
				server_pd->state = TCPPKG_SERVER_STATE_CLOSE;
				//close the socket
				jetnet_close_socket(ss, s->id);
				return;
			}			
			jetnet_cmd_recv_t*cmd_recv = JETNET_GET_MSG_SUB(msg,jetnet_cmd_recv_t);
			char*recv_buf = (char*)&cmd_recv->data[0];
			int recv_len = cmd_recv->len;			
			jetnet_tcppkg_cmd pkg_cmd;
			ret = jetnet_tcppkg_unpack_cmd(recv_buf,recv_len,&pkg_cmd);
			//free the buffer first.
			jetnet_free(msg);
			//check result
			if(ret < 0){
				//we remove it from links
				list_del(&server_pd->link_node);				
				//set up close state
				server_pd->state = TCPPKG_SERVER_STATE_CLOSE;
				//close the socket
				jetnet_close_socket(ss, s->id);
				return;
			}
			if(pkg_cmd.cmd_id != TCPPKG_CMD_VALID_REQ){
				//we remove it from links
				list_del(&server_pd->link_node);
				//set up close state
				server_pd->state = TCPPKG_SERVER_STATE_CLOSE;
				//close the socket
				jetnet_close_socket(ss, s->id);
				return;
			}
			//check the valid_req data... ...
			
			//ok,now we send an valid_ack
			jetnet_tcppkg_cmd ack_cmd;
			ack_cmd.cmd_id = TCPPKG_CMD_VALID_ACK;
			ack_cmd.data.valid_ack.key = 0;
			
			char*send_buf = NULL;
			int send_len = 0;
			if( jetnet_tcppkg_pack_cmd(&ack_cmd, &send_buf, &send_len) < 0){
				//we remove it from links
				list_del(&server_pd->link_node);
				//set up close state
				server_pd->state = TCPPKG_SERVER_STATE_CLOSE;
				//close the socket
				jetnet_close_socket(ss, s->id);
				return;
			}
			jetnet_leid_t owner_leid;
			owner_leid.cno = 0;
			owner_leid.eno = 0;
			if(jetnet_tcppkg_send_pkg(ss,s->id,owner_leid,0,send_buf,send_len,TCPPKG_MESSAGE_TYPE_SYS) < 0){
				//we remove it from links
				list_del(&server_pd->link_node);
				//set up close state
				server_pd->state = TCPPKG_SERVER_STATE_CLOSE;
				//close the socket
				jetnet_close_socket(ss, s->id);
				return;
			}
			//we remove it from links
			list_del(&server_pd->link_node);
			//now we should report the connection notify and setup the state
			jetnet_tcppkg_notify_accept(s->owner_leid, s->id,s->peer_host,s->peer_port,mq);			
			server_pd->state = TCPPKG_SERVER_STATE_TRANS;
			//and then,we should recv package recurrent
			while(1){
				msg = NULL;
				ret = jetnet_tcppkg_recv_pkg(s, NULL, &msg, TCPPKG_MESSAGE_TYPE_APP);
				if(ret == 1)
					return;
				if(ret == -1){				
					//notify close!
					jetnet_tcppkg_notify_close(s->owner_leid, s->id, 0, mq);						
					//set up close state
					server_pd->state = TCPPKG_SERVER_STATE_CLOSE;
					//close the socket
					jetnet_close_socket(ss, s->id);
					return;
				}
				//we get an package
				jetnet_mq_push(mq,msg);
			}
			break;
		}
		case TCPPKG_SERVER_STATE_TRANS:{
			jetnet_msg_t*msg;
			int ret;
			while(1){
				msg = NULL;
				ret = jetnet_tcppkg_recv_pkg(s, rb, &msg,TCPPKG_MESSAGE_TYPE_APP);
				//we set the rb = NULL for next call
				if(rb)
					rb = NULL;
				//check
				if(ret == 1)
					return;
				if(ret == -1){
					//notify unreachable
					jetnet_tcppkg_notify_unreachable(s, mq);					
					//notify close!
					jetnet_tcppkg_notify_close(s->owner_leid, s->id, 0, mq);						
					//set up close state
					server_pd->state = TCPPKG_SERVER_STATE_CLOSE;
					//close the socket
					jetnet_close_socket(ss, s->id);
					return;
				}
				//we get an package
				jetnet_mq_push(mq,msg);
			}
			break;
		}
		case TCPPKG_SERVER_STATE_CLOSE:{
			//close the socket
			jetnet_close_socket(ss, s->id);
			break;
		}
	}
}

void	jetnet_tcppkg_handle_server_close(jetnet_ss_t*ss, jetnet_base_socket_t*s, int errcode, jetnet_mq_t*mq){
	jetnet_tcppkg_server_pd*server_pd = (jetnet_tcppkg_server_pd*)s->ud;
	switch(server_pd->state){
		case TCPPKG_SERVER_STATE_NONE:{			
			assert(!"tcppkg server will not get this NONE state!\n");
			return;
		}		
		case TCPPKG_SERVER_STATE_CONNECTED:{
			//we remove it from links
			list_del(&server_pd->link_node);
			//set up close state
			server_pd->state = TCPPKG_SERVER_STATE_CLOSE;
			//close the socket
			jetnet_close_socket(ss, s->id);
			return;
		}
		case TCPPKG_SERVER_STATE_TRANS:{
			//notify unreachable
			jetnet_tcppkg_notify_unreachable(s, mq);					
			//notify close!
			jetnet_tcppkg_notify_close(s->owner_leid, s->id, 0, mq);						
			//set up close state
			server_pd->state = TCPPKG_SERVER_STATE_CLOSE;
			//close the socket
			jetnet_close_socket(ss, s->id);
			return;
		}
		case TCPPKG_SERVER_STATE_CLOSE:{
			//close the socket
			jetnet_close_socket(ss, s->id);
			return;
		}		
	}
}

void jetnet_tcppkg_ev_handler(jetnet_ss_t*ss, jetnet_pe_t*ev, jetnet_mq_t*mq){
	switch(ev->ev_type){
		case POLL_EV_CONNECT:{
			jetnet_base_socket_t*s = ev->s;
			jetnet_tcppkg_client_pd*client_pd = (jetnet_tcppkg_client_pd*)s->ud;
			if(ev->data.connect.result == 0){//connect fail.
				//notify connect fail
				jetnet_tcppkg_notify_connect(s->owner_leid, s->id,s->peer_host,s->peer_port,0,ev->data.connect.errcode,mq);
				//set up close state
				client_pd->state = TCPPKG_CLIENT_STATE_CLOSE;
				//close the socket
				jetnet_close_socket(ss, s->id);
			}else{//connect succeed
				//we init the parser information
				jetnet_tcppkg_parser_reset(client_pd->pi);
				client_pd->state = TCPPKG_CLIENT_STATE_VALIDING;
				//now we send the valid req
				jetnet_tcppkg_cmd valid_req;
				valid_req.cmd_id = TCPPKG_CMD_VALID_REQ;
				valid_req.data.valid_req.key = 0;
				
				char*send_buf = NULL;
				int send_len = 0;
				if( jetnet_tcppkg_pack_cmd(&valid_req, &send_buf, &send_len) < 0){
					//notify connect fail
					jetnet_tcppkg_notify_connect(s->owner_leid, s->id,s->peer_host,s->peer_port,0,jetnet_errno,mq);
					//set up close state
					client_pd->state = TCPPKG_CLIENT_STATE_CLOSE;					
					//close the socket
					jetnet_close_socket(ss, s->id);
					break;
				}
				jetnet_leid_t owner_leid;
				owner_leid.cno = 0;
				owner_leid.eno = 0;
				if(jetnet_tcppkg_send_pkg(ss,s->id,owner_leid,0,send_buf,send_len,TCPPKG_MESSAGE_TYPE_SYS) < 0){
					//notify connect fail
					jetnet_tcppkg_notify_connect(s->owner_leid, s->id,s->peer_host,s->peer_port,0,jetnet_errno,mq);
					//set up close state
					client_pd->state = TCPPKG_CLIENT_STATE_CLOSE;					
					//close the socket
					jetnet_close_socket(ss, s->id);
					break;
				}				
			}
			break;
		}
		case POLL_EV_ACCEPT:{			
			jetnet_tcppkg_server_pd*server_pd = (jetnet_tcppkg_server_pd*)jetnet_tcppkg_malloc_server_pd();
			server_pd->state = TCPPKG_SERVER_STATE_CONNECTED;
			LIST_NODE_INIT(&server_pd->link_node);
			jetnet_tcppkg_parser_reset(server_pd->pi);
			ev->s->ud = server_pd;
			ev->s->free_func = jetnet_tcppkg_free_server_pd;
			//add to the links
			list_add(&server_pd->link_node, &jetnet_g_tcppkg->link_list);
			break;
		}		
		case POLL_EV_RECV:{
			jetnet_base_socket_t*s = ev->s;
			if(SOCKET_R_CLIENT == s->socket_r){
				jetnet_tcppkg_handle_client_recv(ss, s, ev->data.recv.rb, mq);
			}else if(SOCKET_R_SERVER == s->socket_r){
				jetnet_tcppkg_handle_server_recv(ss, s, ev->data.recv.rb, mq);
			}else{
				assert(!"can't recv data in this socket role!\n");
			}
			break;
		}
		case POLL_EV_CLOSE:{
			jetnet_base_socket_t*s = ev->s;
			if(SOCKET_R_CLIENT == s->socket_r){
				jetnet_tcppkg_handle_client_close(ss, s, ev->data.close.errcode, mq);
			}else if(SOCKET_R_SERVER == s->socket_r){
				jetnet_tcppkg_handle_server_close(ss, s, ev->data.close.errcode, mq);
			}else if(SOCKET_R_LISTENER == s->socket_r){
				//the listener is close.
				jetnet_tcppkg_notify_close(s->owner_leid, s->id, 1, mq);
				//close
				jetnet_close_socket(ss, s->id);
			}
			break;
		}
	}
}

inline int jetnet_tcppkg_listen(jetnet_ss_t*ss, jetnet_leid_t owner_leid, const char * host, int port, int backlog){
	int ret = jetnet_tcp_listen(ss, owner_leid, host, port, backlog);
	if(ret >= 0){
		jetnet_base_socket_t*s = jetnet_get_socket(ss, ret);
		jetnet_base_pd*listenser_pd = (jetnet_base_pd*)jetnet_malloc(sizeof(jetnet_base_pd));
		listenser_pd->protocol = JETNET_PROTO_TCPPKG;
		s->ud = listenser_pd;
		s->free_func = NULL;
	}
	return ret;
}

int jetnet_tcppkg_connect(jetnet_ss_t*ss, jetnet_leid_t owner_leid, const char * host, int port){
	int ret = jetnet_tcp_connect(ss, owner_leid, host, port);
	if(ret >= 0){
		jetnet_base_socket_t*s = jetnet_get_socket(ss, ret);
		jetnet_tcppkg_client_pd*client_pd = (jetnet_tcppkg_client_pd*)jetnet_tcppkg_malloc_client_pd();
		client_pd->pi = (jetnet_tcppkg_pi*)jetnet_malloc(sizeof(jetnet_tcppkg_pi));
		client_pd->base.protocol = JETNET_PROTO_TCPPKG;
		client_pd->state = TCPPKG_CLIENT_STATE_CONNECTING;
		//reset the pi
		jetnet_tcppkg_parser_reset(client_pd->pi);			
		s->ud = client_pd;
		s->free_func = &jetnet_tcppkg_free_client_pd;			
		//
		if(jetnet_tcp_is_connected(ss, ret)){
			//now we should send an valid_req package
			client_pd->state = TCPPKG_CLIENT_STATE_VALIDING;
			//now we send the valid req
			jetnet_tcppkg_cmd valid_req;
			valid_req.cmd_id = TCPPKG_CMD_VALID_REQ;
			valid_req.data.valid_req.key = 0;
			
			char*send_buf = NULL;
			int send_len = 0;
			if( jetnet_tcppkg_pack_cmd(&valid_req, &send_buf, &send_len) < 0){
				//set up close state
				client_pd->state = TCPPKG_CLIENT_STATE_CLOSE;
				//close the socket
				jetnet_close_socket(ss, s->id);
				return -1;
			}
			jetnet_leid_t owner_leid;
			owner_leid.cno = 0;
			owner_leid.eno = 0;
			if(jetnet_tcppkg_send_pkg(ss,s->id,owner_leid,0,send_buf,send_len,TCPPKG_MESSAGE_TYPE_SYS) < 0){
				//set up close state
				client_pd->state = TCPPKG_CLIENT_STATE_CLOSE;				
				//close the socket
				jetnet_close_socket(ss, s->id);
				return -1;
			}
		}
	}
	return ret;
}

int jetnet_tcppkg_send(jetnet_ss_t*ss, int id, jetnet_leid_t owner_leid, uint32_t seq_id, void* data, int len){
	jetnet_base_socket_t*s = jetnet_get_socket(ss, id);
	if(!s){
		jetnet_errno = ELOCATESOCKET;
		jetnet_free(data);
		return -1;
	}
	if(s->socket_r == SOCKET_R_CLIENT){
		jetnet_tcppkg_client_pd*client_pd = (jetnet_tcppkg_client_pd*)s->ud;
		if(!client_pd){
			jetnet_errno = ESOCKETSTATE;
			jetnet_free(data);
			return -1;
		}
		switch(client_pd->state){
			case TCPPKG_CLIENT_STATE_NONE:
			case TCPPKG_CLIENT_STATE_CONNECTING:
			case TCPPKG_CLIENT_STATE_VALIDING:
			case TCPPKG_CLIENT_STATE_CLOSE:{
				jetnet_errno = ESOCKETSTATE;
				jetnet_free(data);
				return -1;
			}
			case TCPPKG_CLIENT_STATE_TRANS:{
				jetnet_iov_t vec[JETNET_MAX_IOVEC];
				int vec_count = 0;			
				if(jetnet_tcppkg_formatter(data, len, vec,&vec_count) < 0){
					jetnet_free(data);
					jetnet_errno = ESERIALIZE;
					return -1;
				}
				return jetnet_tcp_sendv(ss, id, owner_leid, seq_id, vec ,vec_count);
			}
		}
	}else if(s->socket_r == SOCKET_R_SERVER){
		jetnet_tcppkg_server_pd*server_pd = (jetnet_tcppkg_server_pd*)s->ud;
		if(!server_pd){
			jetnet_errno = ESOCKETSTATE;
			jetnet_free(data);
			return -1;
		}
		switch(server_pd->state){
			case TCPPKG_SERVER_STATE_NONE:
			case TCPPKG_SERVER_STATE_CONNECTED:
			case TCPPKG_SERVER_STATE_CLOSE:{
				jetnet_errno = ESOCKETSTATE;
				jetnet_free(data);
				return -1;
			}
			case TCPPKG_SERVER_STATE_TRANS:{
				jetnet_iov_t vec[JETNET_MAX_IOVEC];
				int vec_count = 0;			
				if(jetnet_tcppkg_formatter(data, len, vec,&vec_count) < 0){
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

void jetnet_tcppkg_close(jetnet_ss_t*ss, int id){
	jetnet_base_socket_t*s = jetnet_get_socket(ss, id);
	if(!s)
		return;
	if(s->socket_r == SOCKET_R_CLIENT){
		jetnet_tcppkg_client_pd*client_pd = (jetnet_tcppkg_client_pd*)s->ud;
		client_pd->state = TCPPKG_CLIENT_STATE_CLOSE;
	}else if(s->socket_r == SOCKET_R_SERVER){
		jetnet_tcppkg_server_pd*server_pd = (jetnet_tcppkg_server_pd*)s->ud;
		server_pd->state = TCPPKG_SERVER_STATE_CLOSE;
	}
	jetnet_close_socket(ss, id);
}

jetnet_proto_t* jetnet_tcppkg_create(){
	if(jetnet_g_tcppkg)
		return &jetnet_g_tcppkg->base;	
	jetnet_g_tcppkg = (jetnet_net_tcppkg*)jetnet_malloc(sizeof(jetnet_net_tcppkg));
	jetnet_g_tcppkg->base.f_malloc_pi = &jetnet_tcppkg_malloc_pi;
	jetnet_g_tcppkg->base.free_pi = &jetnet_tcppkg_free_pi;
	jetnet_g_tcppkg->base.parser_reset = &jetnet_tcppkg_parser_reset;
	jetnet_g_tcppkg->base.parser = &jetnet_tcppkg_parser;
	jetnet_g_tcppkg->base.poll = &jetnet_tcppkg_poll;
	jetnet_g_tcppkg->base.ev_handler = &jetnet_tcppkg_ev_handler;
	jetnet_g_tcppkg->base.listen = &jetnet_tcppkg_listen;	
	jetnet_g_tcppkg->base.connect = &jetnet_tcppkg_connect;
	jetnet_g_tcppkg->base.send = &jetnet_tcppkg_send;
	jetnet_g_tcppkg->base.close = &jetnet_tcppkg_close;	
	INIT_LIST_HEAD(&jetnet_g_tcppkg->link_list);
	return &jetnet_g_tcppkg->base;
}