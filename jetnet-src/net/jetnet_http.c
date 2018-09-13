#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include <stdio.h>

#include "jetnet_http.h"
#include "jetnet_errno.h"
#include "jetnet_time.h"
#include "list.h"
#include "jetnet_malloc.h"
#include "http_parser.h"
#include "jetnet_macro.h"
#include "jetnet_msg.h"

#define JETNET_MAX_HEADER_FIELD 128
#define HTTP_MAX_BODY_LEN		1048576 //1M
#define HTTP_DEFAULT_ALIVE_TIME 15000

//define client state
enum{
	HTTP_CLIENT_STATE_NONE = 0,
	HTTP_CLIENT_STATE_CONNECTING,
	HTTP_CLIENT_STATE_TRANS,
	HTTP_CLIENT_STATE_CLOSE,
};

//define server state
enum{
	HTTP_SERVER_STATE_NONE = 0,
	HTTP_SERVER_STATE_TRANS,
	HTTP_SERVER_STATE_CLOSE,
};

//define http parser step
enum{
	HTTP_PARSE_STEP_NONE = 0,
	HTTP_PARSE_STEP_MESSAGE_BEGIN,
	HTTP_PARSE_STEP_URL,
	HTTP_PARSE_STEP_STATUS,
	HTTP_PARSE_STEP_HEADER_FIELD,
	HTTP_PARSE_STEP_HEADER_VALUE,
	HTTP_PARSE_STEP_HEADER_COMPLETE,
	HTTP_PARSE_STEP_BODY,
	HTTP_PARSE_STEP_MESSAGE_COMPLETE,
	HTTP_PARSE_STEP_CHUNK_HEADER,
	HTTP_PARSE_STEP_CHUNK_COMPLETE,
	HTTP_PARSE_STEP_ERROR,
};  

typedef struct jetnet_http_pr_t{
	size_t current_offset; //the current parser offset from the message begin
	const char* current_parser_data_addr; //the current parser data's address,we use this to calculate the data offset
	
	int	parser_step; //current step of parsing
	int body_offset;
	int body_len;
	bool get_header_complete;
	bool get_message_complete;
	
	bool should_keep_alive;//information for keep_alive request for this socket
	size_t url_offset; //the url information for this parsed,url_len=0 means hasn't url field found
	size_t url_len;
	unsigned short http_major;
	unsigned short http_minor;
	unsigned int status_code; /* responses only */
	unsigned int method;       /* requests only */
	unsigned int upgrade;

	unsigned int field_num; //the field num had beed parsed
	jetnet_http_field_t	field_infos[JETNET_MAX_HEADER_FIELD]; //fields had been parsed
}jetnet_http_pr;

typedef struct jetnet_http_pi_t{
	http_parser parser;
	jetnet_http_pr pr;
}jetnet_http_pi_t;

//define the http protocol data
typedef struct jetnet_http_pd_t{
	jetnet_base_pd base;
	int state;
	long int respone_time;
	bool keep_alive;
	struct list_head respone_link_node;
	jetnet_http_pi_t* pi;
}jetnet_http_pd_t;

//define the http protocol object
typedef struct jetnet_net_http_t{
	jetnet_proto_t base;
	int f,f1,f3,f5,f6;
	struct list_head respone_link_list;
	int a1,a2,a3,a4,a5;
}jetnet_net_http_t;

//the globle http object
jetnet_net_http_t*jetnet_g_http = NULL;
http_parser_settings* http_settings;

inline size_t jetnet_cal_adress_offset(jetnet_http_pr* pr, const char* at){
	return pr->current_offset + (size_t)(at - pr->current_parser_data_addr);
}

int jetnet_on_message_begin(http_parser* parser) {
	jetnet_http_pr* pr = (jetnet_http_pr*)parser->data;
	pr->parser_step = HTTP_PARSE_STEP_MESSAGE_BEGIN;
	return 0;
}

int jetnet_on_url(http_parser* parser, const char* at, size_t length) {
	jetnet_http_pr* pr = (jetnet_http_pr*)parser->data;
	pr->parser_step = HTTP_PARSE_STEP_URL;
	pr->url_offset = jetnet_cal_adress_offset(pr,at);
	pr->url_len = length;
	return 0;
}

int jetnet_on_status(http_parser* parser, const char* at, size_t length) {
	jetnet_http_pr* pr = (jetnet_http_pr*)parser->data;
	pr->parser_step = HTTP_PARSE_STEP_STATUS;
	pr->status_code = parser->status_code;
	return 0;
}

int jetnet_on_header_field(http_parser* parser, const char* at, size_t length) {
	//we should check the state first, because the parser may trigger many time for one header field
	jetnet_http_pr* pr = (jetnet_http_pr*)parser->data;
	if(pr->parser_step == HTTP_PARSE_STEP_HEADER_FIELD){
		//in this situation, the at == pr->current_parser_data_addr
		assert(at == pr->current_parser_data_addr);
		assert(pr->field_num > 0);
		pr->field_infos[pr->field_num - 1].key_len += length;
	}else{
		//error and pause parse
		if(pr->field_num >= JETNET_MAX_HEADER_FIELD)
			return 1;
		pr->field_infos[pr->field_num].key_offset = jetnet_cal_adress_offset(pr,at);
		pr->field_infos[pr->field_num].key_len = length;
		pr->field_num++;
		pr->parser_step = HTTP_PARSE_STEP_HEADER_FIELD;
	}
	return 0;
}

int jetnet_on_header_value(http_parser* parser, const char* at, size_t length) {
	//we should check the state first, because the parser may trigger many time for one header field
	jetnet_http_pr* pr = (jetnet_http_pr*)parser->data;
	if(pr->parser_step == HTTP_PARSE_STEP_HEADER_VALUE){
		//in this situation, the at == pr->current_parser_data_addr
		assert(at == pr->current_parser_data_addr);
		assert(pr->field_num > 0);
		pr->field_infos[pr->field_num - 1].value_len += length;
	}else{
		pr->field_infos[pr->field_num - 1].value_offset = jetnet_cal_adress_offset(pr,at);
		pr->field_infos[pr->field_num - 1].value_len = length;
		pr->parser_step = HTTP_PARSE_STEP_HEADER_VALUE;
	}	
	return 0;
}
	
int jetnet_on_headers_complete(http_parser* parser){
	jetnet_http_pr* pr = (jetnet_http_pr*)parser->data;
	pr->parser_step = HTTP_PARSE_STEP_HEADER_COMPLETE;
	pr->get_header_complete = true;
	pr->http_major = parser->http_major;
	pr->http_minor = parser->http_minor;
	pr->status_code = parser->status_code;
	pr->method = parser->method;
	pr->upgrade = parser->upgrade;
	pr->should_keep_alive = http_should_keep_alive(parser) == 0 ? false : true;	
	return 0;
}

int jetnet_on_body(http_parser* parser, const char* at, size_t length) {
	jetnet_http_pr* pr = (jetnet_http_pr*)parser->data;
	pr->parser_step = HTTP_PARSE_STEP_BODY;
	if(pr->body_offset == -1)
		pr->body_offset = jetnet_cal_adress_offset(pr,at);
	pr->body_len += length;
	return 0;
}

int jetnet_on_message_complete(http_parser* parser){
	jetnet_http_pr* pr = (jetnet_http_pr*)parser->data;
	pr->parser_step = HTTP_PARSE_STEP_MESSAGE_COMPLETE;	
	pr->get_message_complete = true;
	//we need to pause it now
	http_parser_pause(parser,1);
	return 0;
}

int jetnet_on_chunk_header(http_parser* parser){
	jetnet_http_pr* pr = (jetnet_http_pr*)parser->data;
	pr->parser_step = HTTP_PARSE_STEP_CHUNK_HEADER;
	return 0;
}

int jetnet_on_chunk_complete(http_parser* parser){
	jetnet_http_pr* pr = (jetnet_http_pr*)parser->data;
	pr->parser_step = HTTP_PARSE_STEP_CHUNK_COMPLETE;
	return 0;
}

void jetnet_init_http_callback_setting(http_parser_settings*settings){
  settings->on_message_begin = &jetnet_on_message_begin;
  settings->on_url = &jetnet_on_url;
  settings->on_status = &jetnet_on_status;
  settings->on_header_field = &jetnet_on_header_field;
  settings->on_header_value = &jetnet_on_header_value;
  settings->on_headers_complete = &jetnet_on_headers_complete;
  settings->on_body = &jetnet_on_body;
  settings->on_message_complete = &jetnet_on_message_complete;
  settings->on_chunk_header = &jetnet_on_chunk_header;
  settings->on_chunk_complete = &jetnet_on_chunk_complete;
}
	
void jetnet_init_http_pr(jetnet_http_pr*pr){
	pr->current_offset = 0;
	pr->current_parser_data_addr = NULL;
	pr->parser_step = HTTP_PARSE_STEP_NONE;
	pr->body_offset = -1;
	pr->body_len = 0;
	pr->get_header_complete = false;
	pr->get_message_complete = false;
	pr->should_keep_alive = false;
	pr->url_offset = -1;
	pr->url_len = 0;
	pr->http_major = 0;
	pr->http_minor = 0;
	pr->status_code = 0;
	pr->method = 0;
	pr->upgrade = 0;
	memset(pr->field_infos, 0, sizeof(pr->field_infos));	
}

void* jetnet_http_malloc_pi(int socket_r){
	if(socket_r == SOCKET_R_CLIENT){
		jetnet_http_pi_t*pi = (jetnet_http_pi_t*)jetnet_malloc(sizeof(jetnet_http_pi_t));
		http_parser_init(&pi->parser, HTTP_RESPONSE);
		//http_parser_init(&pi->parser, HTTP_BOTH);		
		jetnet_init_http_pr(&pi->pr);
		pi->parser.data = &pi->pr;
		return pi;
	}
	jetnet_http_pi_t*pi = (jetnet_http_pi_t*)jetnet_malloc(sizeof(jetnet_http_pi_t));
	http_parser_init(&pi->parser, HTTP_REQUEST);
	//http_parser_init(&pi->parser, HTTP_BOTH);
	jetnet_init_http_pr(&pi->pr);
	pi->parser.data = &pi->pr;
	return pi;
}

void jetnet_http_free_pi(void*pi){
	if(!pi)
		return;
	jetnet_free(pi);
}

void jetnet_http_parser_reset(void*pi){
	jetnet_http_pi_t*http_pi = (jetnet_http_pi_t*)pi;
	if(!http_pi)
		return;
	jetnet_init_http_pr(&http_pi->pr);
}

void jetnet_http_release(){
	if(jetnet_g_http){
		jetnet_free(jetnet_g_http);
		jetnet_g_http = NULL;
	}
	if(http_settings){
		jetnet_free(http_settings);
		http_settings = NULL;
	}
}

void* jetnet_http_malloc_pd(int socket_r){
	jetnet_http_pd_t*pd = (jetnet_http_pd_t*)jetnet_malloc(sizeof(jetnet_http_pd_t));
	pd->base.protocol = JETNET_PROTO_HTTP;
	if(socket_r == SOCKET_R_SERVER)
		pd->state = HTTP_SERVER_STATE_NONE;
	else
		pd->state = HTTP_CLIENT_STATE_NONE;
	pd->keep_alive = false;
	pd->respone_time = 0;
	INIT_LIST_HEAD(&pd->respone_link_node);
	pd->pi = (jetnet_http_pi_t*)jetnet_http_malloc_pi(socket_r);
	return pd;
}

void jetnet_http_free_pd(void*pd){
	jetnet_http_pd_t*http_pd = (jetnet_http_pd_t*)pd;
	if(http_pd->pi){
		jetnet_http_free_pi(http_pd->pi);
	}
	jetnet_free(http_pd);
}

size_t jetnet_http_parser_execute_wraper(http_parser *parser,
                           const http_parser_settings *settings,
                           const char *data,
                           size_t len){
	jetnet_http_pr*pr = (jetnet_http_pr*)parser->data;
	pr->current_parser_data_addr = data;
	size_t nparse = http_parser_execute(parser,settings,data,len);
	pr->current_offset += nparse;
	return nparse;
}
	
/*
-1:error format
0:succeed,has get an message
1:don't confirm any thing, need wait for more data
2:party confirm and wait for more data
3:had confirm, and need more data for an complete message
*/
int jetnet_http_parser(jetnet_rbl_t *rbl, jetnet_rb_t *rb, void*pi){
	jetnet_http_pi_t*http_pi = (jetnet_http_pi_t*)pi;
	if(!http_pi)
		return -1;
	if(HTTP_PARSE_STEP_MESSAGE_COMPLETE == http_pi->pr.parser_step)
		return 0;
	if(HTTP_PARSE_STEP_ERROR == http_pi->pr.parser_step)
		return -1;
	//if the step is none,
	if(HTTP_PARSE_STEP_NONE == http_pi->pr.parser_step)
		rb = NULL;
	if(rb){
		jetnet_http_parser_execute_wraper(&http_pi->parser, http_settings, rb->buffer, rb->size);
		//check the parser error code
		if(HTTP_PARSER_ERRNO(&http_pi->parser) == HPE_OK || HTTP_PARSER_ERRNO(&http_pi->parser) == HPE_PAUSED){
			if(http_pi->pr.parser_step == HTTP_PARSE_STEP_MESSAGE_COMPLETE)
				return 0;
			if(http_pi->pr.parser_step >= HTTP_PARSE_STEP_HEADER_COMPLETE)
				return 3;
			if(http_pi->pr.parser_step >= HTTP_PARSE_STEP_HEADER_FIELD)
				return 2;
			return 1;
		}else{
			http_pi->pr.parser_step = HTTP_PARSE_STEP_ERROR;
			return -1;
		}
	}else{
		jetnet_rb_t*first = rbl->head;
		int offset = rbl->offset;
		while(first && first->size <= offset){
			offset -= first->size;
			first = first->next;
		}
		if(!first)
			return -1;
		do{			
			jetnet_http_parser_execute_wraper(&http_pi->parser, http_settings, first->buffer + offset, first->size - offset);
			//check the parser error code
			if(HTTP_PARSER_ERRNO(&http_pi->parser) == HPE_OK || HTTP_PARSER_ERRNO(&http_pi->parser) == HPE_PAUSED){
				if(http_pi->pr.parser_step == HTTP_PARSE_STEP_MESSAGE_COMPLETE)
					return 0;
			}else{
				http_pi->pr.parser_step = HTTP_PARSE_STEP_ERROR;
				return -1;
			}
			first = first->next;
			offset = 0;
		}while(first);
		if(http_pi->pr.parser_step >= HTTP_PARSE_STEP_HEADER_COMPLETE)
			return 3;
		if(http_pi->pr.parser_step >= HTTP_PARSE_STEP_HEADER_FIELD)
			return 2;
		return 1;
	}
	return -1;
}

void jetnet_http_poll(jetnet_ss_t*ss, jetnet_mq_t*mq){
	//we should check the unvalid socket list
	if(!jetnet_g_http)
		return;	
	while(!list_empty(&jetnet_g_http->respone_link_list)){
		jetnet_http_pd_t*pd = list_entry(jetnet_g_http->respone_link_list.next, jetnet_http_pd_t, respone_link_node);
		jetnet_base_socket_t*s = container_of(((void*)pd),jetnet_base_socket_t,ud);
		if(s->socket_r == SOCKET_R_SERVER){
			if( jetnet_time_now() - pd->respone_time < HTTP_DEFAULT_ALIVE_TIME)
				break;
			//remove from links
			list_del_init(&pd->respone_link_node);
			//setup state
			pd->state = HTTP_SERVER_STATE_CLOSE;
			//close socket
			jetnet_close_socket(ss,s->id);
		}else{
			//remove from links
			list_del_init(&pd->respone_link_node);
		}
	}
}

void	jetnet_http_notify_connect(jetnet_leid_t dst_leid, int id, char*host , int port , int result , int errcode , jetnet_mq_t*mq){
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
	sub_cmd->protocol = JETNET_PROTO_HTTP;
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

jetnet_msg_t* jetnet_gen_http_recv_msg(jetnet_base_socket_t*s, jetnet_http_pr*pr){
	int http_data_len = JETNET_HTTP_HEADER_FIELDS_OFFSET + pr->field_num*sizeof(jetnet_http_field_t) + pr->current_offset;
	int cmd_len = JETNET_GET_VAR_LEN(jetnet_cmd_recv_t,http_data_len);
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
	sub_cmd->protocol = JETNET_PROTO_HTTP;
	sub_cmd->len = http_data_len;
	
	jetnet_http_data_t*data = (jetnet_http_data_t*)&sub_cmd->data[0];
	data->get_header_complete = pr->get_header_complete;
	data->get_message_complete = pr->get_message_complete;
	if(s->socket_r == SOCKET_R_CLIENT)
		data->be_request = false;
	else
		data->be_request = true;
	data->should_keep_alive = pr->should_keep_alive;
	data->http_major = pr->http_major;
	data->http_minor = pr->http_minor;
	data->method = pr->method;
	data->url_offset = pr->url_offset;
	data->url_len = pr->url_len;
	data->status_code = pr->status_code;
	data->body_offset = pr->body_offset;
	data->body_len = pr->body_len;
	data->http_data_size = pr->current_offset;
	data->field_num = pr->field_num;
	memcpy(data->field_infos,pr->field_infos,pr->field_num*sizeof(jetnet_http_field_t));
	if(jetnet_rbl_read(&s->rbl, (char*)data + JETNET_HTTP_HEADER_FIELDS_OFFSET + pr->field_num*sizeof(jetnet_http_field_t), pr->current_offset) != pr->current_offset){
		jetnet_free(msg_temp);
		return NULL;
	}	
	return msg_temp;
}

void	jetnet_http_handle_client_recv(jetnet_ss_t*ss, jetnet_base_socket_t*s,jetnet_rb_t*rb,jetnet_mq_t*mq){
	jetnet_http_pd_t*pd = (jetnet_http_pd_t*)s->ud;
	switch(pd->state){
		case HTTP_CLIENT_STATE_NONE:{
			assert(!"http client will recv data on none state!\n");
			return;
		}
		case HTTP_CLIENT_STATE_CONNECTING:{
			assert(!"http client will recv data on connecting state!\n");
			return;
		}
		case HTTP_CLIENT_STATE_TRANS:{
			do{
				int ret = jetnet_http_parser(&s->rbl, rb, pd->pi);
				if(ret == -1){
					//set up close state
					pd->state = HTTP_CLIENT_STATE_CLOSE;
					//close the socket
					jetnet_close_socket(ss, s->id);						
					break;
				}else if(ret == 0){
					jetnet_msg_t*msg = jetnet_gen_http_recv_msg(s,&pd->pi->pr);
					if(!msg){
						//set up close state
						pd->state = HTTP_CLIENT_STATE_CLOSE;
						//close the socket
						jetnet_close_socket(ss, s->id);						
						break;
					}
					jetnet_mq_push(mq,msg);
					jetnet_http_parser_reset(pd->pi);
				}else if(ret == 1 || ret == 2){
					break;
				}else if(ret == 3){//we handle the truck mode
					jetnet_http_pr*pr = &pd->pi->pr;
					if(pr->body_len >= HTTP_MAX_BODY_LEN){
						jetnet_msg_t*msg = jetnet_gen_http_recv_msg(s,pr);
						if(!msg){
							//set up close state
							pd->state = HTTP_CLIENT_STATE_CLOSE;
							//close the socket
							jetnet_close_socket(ss, s->id);						
							break;
						}
						jetnet_mq_push(mq,msg);
						jetnet_http_parser_reset(pd->pi);
					}
					break;
				}
				rb = NULL;
			}while(1);
			break;
		}
		case HTTP_CLIENT_STATE_CLOSE:{
			//close the socket
			jetnet_close_socket(ss, s->id);
			break;
		}
	}
}

void	jetnet_http_handle_client_close(jetnet_ss_t*ss, jetnet_base_socket_t*s, int errcode, jetnet_mq_t*mq){
	jetnet_http_pd_t*pd = (jetnet_http_pd_t*)s->ud;
	switch(pd->state){
		case HTTP_CLIENT_STATE_NONE:{
			assert(!"http client will not get this NONE state!\n");
			return;
		}
		case HTTP_CLIENT_STATE_CONNECTING:{
			//notify connect fail!
			jetnet_http_notify_connect(s->owner_leid, s->id,s->peer_host,s->peer_port,0,errcode,mq);
			//set up close state
			pd->state = HTTP_CLIENT_STATE_CLOSE;
			//close the socket
			jetnet_close_socket(ss, s->id);
			return;
		}
		case HTTP_CLIENT_STATE_TRANS:{
			jetnet_http_pr*pr = &pd->pi->pr;
			//Note we pass recved==0 to signal that EOF has been received.
			if(pr->parser_step != HTTP_PARSE_STEP_ERROR &&
				pr->parser_step > HTTP_PARSE_STEP_HEADER_COMPLETE && 
					pr->parser_step < HTTP_PARSE_STEP_MESSAGE_COMPLETE ){
				http_parser_execute(&pd->pi->parser,http_settings,NULL,0);
				if(pr->parser_step == HTTP_PARSE_STEP_MESSAGE_COMPLETE){
					jetnet_msg_t*msg = jetnet_gen_http_recv_msg(s,&pd->pi->pr);
					if(msg)
						jetnet_mq_push(mq,msg);					
				}
			}	
			//notify close!
			//jetnet_http_notify_close(s->id, false, mq);						
			//set up close state
			pd->state = HTTP_CLIENT_STATE_CLOSE;
			//close the socket
			jetnet_close_socket(ss, s->id);
			return;
		}
		case HTTP_CLIENT_STATE_CLOSE:{
			//close the socket
			jetnet_close_socket(ss, s->id);
			return;
		}		
	}
}

void	jetnet_http_handle_server_recv(jetnet_ss_t*ss, jetnet_base_socket_t*s, jetnet_rb_t*rb, jetnet_mq_t*mq){
	jetnet_http_pd_t*pd = (jetnet_http_pd_t*)s->ud;
	switch(pd->state){
		case HTTP_SERVER_STATE_NONE:{
			assert(!"http server will not get this NONE state!\n");
			return;	
		}
		case HTTP_SERVER_STATE_TRANS:{
			do{
				int ret = jetnet_http_parser(&s->rbl, rb, pd->pi);
				if(ret == -1){
					//set up close state
					pd->state = HTTP_SERVER_STATE_CLOSE;
					//close the socket
					jetnet_close_socket(ss, s->id);						
					break;
				}else if(ret == 0){
					//set up keep_alive flag
					//pd->should_keep_alive = pd->pi->pr.should_keep_alive;??
					jetnet_msg_t*msg = jetnet_gen_http_recv_msg(s,&pd->pi->pr);
					if(!msg){
						//set up close state
						pd->state = HTTP_SERVER_STATE_CLOSE;
						//close the socket
						jetnet_close_socket(ss, s->id);						
						break;
					}
					jetnet_mq_push(mq,msg);
					jetnet_http_parser_reset(pd->pi);
					if(s->rbl.head == NULL)
						break;
				}else if(ret == 1 || ret == 2){
					break;
				}else if(ret == 3){//we handle the truck mode
					jetnet_http_pr*pr = &pd->pi->pr;
					if(pr->body_len >= HTTP_MAX_BODY_LEN){					
						jetnet_msg_t*msg = jetnet_gen_http_recv_msg(s,pr);
						if(!msg){
							//set up close state
							pd->state = HTTP_SERVER_STATE_CLOSE;
							//close the socket
							jetnet_close_socket(ss, s->id);						
							break;
						}
						jetnet_mq_push(mq,msg);
						jetnet_http_parser_reset(pd->pi);
					}
					break;
				}
				rb = NULL;
			}while(1);
			break;
		}
		case HTTP_SERVER_STATE_CLOSE:{
			//close the socket
			jetnet_close_socket(ss, s->id);
			break;
		}
	}
}

void	jetnet_http_handle_server_close(jetnet_ss_t*ss, jetnet_base_socket_t*s, int errcode, jetnet_mq_t*mq){
	jetnet_http_pd_t*pd = (jetnet_http_pd_t*)s->ud;
	switch(pd->state){
		case HTTP_SERVER_STATE_NONE:{
			assert(!"http server will not get this NONE state!\n");
			return;
		}
		case HTTP_SERVER_STATE_TRANS:{
			//notify close!
			//jetnet_http_notify_close(s->id, false, mq);						
			//set up close state
			pd->state = HTTP_SERVER_STATE_CLOSE;
			//close the socket
			jetnet_close_socket(ss, s->id);
			return;
		}
		case HTTP_SERVER_STATE_CLOSE:{
			//close the socket
			jetnet_close_socket(ss, s->id);
			return;
		}		
	}
}

void jetnet_http_ev_handler(jetnet_ss_t*ss, jetnet_pe_t*ev, jetnet_mq_t*mq){
	switch(ev->ev_type){
		case POLL_EV_CONNECT:{
			jetnet_base_socket_t*s = ev->s;
			jetnet_http_pd_t*pd = (jetnet_http_pd_t*)s->ud;
			if(ev->data.connect.result == 0){//connect fail.
				//notify connect fail
				jetnet_http_notify_connect(s->owner_leid, s->id,s->peer_host,s->peer_port,0,ev->data.connect.errcode,mq);
				//set up close state
				pd->state = HTTP_CLIENT_STATE_CLOSE;
				//close the socket
				jetnet_close_socket(ss, s->id);
			}else{//connect succeed
				//we init the parser information
				pd->state = HTTP_CLIENT_STATE_TRANS;
				jetnet_http_notify_connect(s->owner_leid, s->id,s->peer_host,s->peer_port,1,0,mq);
			}
			break;
		}
		case POLL_EV_ACCEPT:{
			jetnet_http_pd_t*server_pd = (jetnet_http_pd_t*)jetnet_http_malloc_pd(ev->s->socket_r);
			server_pd->state = HTTP_SERVER_STATE_TRANS;
			ev->s->ud = server_pd;
			ev->s->free_func = &jetnet_http_free_pd;
			//jetnet_http_notify_accept(ev->s->id,ev->s->peer_host,ev->s->peer_port,mq);
			break;
		}
		case POLL_EV_RECV:{
			jetnet_base_socket_t*s = ev->s;
			if(SOCKET_R_CLIENT == s->socket_r){
				jetnet_http_handle_client_recv(ss, s, ev->data.recv.rb, mq);
			}else if(SOCKET_R_SERVER == s->socket_r){
				jetnet_http_handle_server_recv(ss, s, ev->data.recv.rb, mq);
			}else{
				assert(!"can't recv data in this socket role!\n");
			}
			break;
		}
		case POLL_EV_CLOSE:{
			jetnet_base_socket_t*s = ev->s;
			if(SOCKET_R_CLIENT == s->socket_r){
				jetnet_http_handle_client_close(ss, s, ev->data.close.errcode, mq);
			}else if(SOCKET_R_SERVER == s->socket_r){
				jetnet_http_handle_server_close(ss, s, ev->data.close.errcode, mq);
			}else if(SOCKET_R_LISTENER == s->socket_r){
				//the listener is close.
				//jetnet_http_notify_close(s->id, true, mq);
				//close
				jetnet_close_socket(ss, s->id);
			}
			break;
		}
	}
}

int jetnet_http_listen(jetnet_ss_t*ss, jetnet_leid_t owner_leid, const char * host, int port, int backlog){
	int ret = jetnet_tcp_listen(ss, owner_leid, host, port, backlog);
	if(ret >= 0){
		jetnet_base_socket_t*s = jetnet_get_socket(ss, ret);
		jetnet_base_pd*listenser_pd = (jetnet_base_pd*)jetnet_malloc(sizeof(jetnet_base_pd));
		listenser_pd->protocol = JETNET_PROTO_HTTP;		
		s->ud = listenser_pd;
		s->free_func = NULL;
	}
	return ret;
}

int jetnet_http_connect(jetnet_ss_t*ss, jetnet_leid_t owner_leid, const char * host, int port){
	int ret = jetnet_tcp_connect(ss, owner_leid, host, port);
	if(ret >= 0){
		jetnet_base_socket_t*s = jetnet_get_socket(ss, ret);
		jetnet_http_pd_t*pd = (jetnet_http_pd_t*)jetnet_http_malloc_pd(s->socket_r);
		pd->base.protocol = JETNET_PROTO_HTTP;
		pd->state = HTTP_CLIENT_STATE_CONNECTING;		
		s->ud = pd;
		s->free_func = &jetnet_http_free_pd;			
		//
		if(jetnet_tcp_is_connected(ss,ret)){
			//now we should send an valid_req package
			pd->state = HTTP_CLIENT_STATE_TRANS;
		}
	}
	return ret;
}

int jetnet_http_send(jetnet_ss_t*ss, int id, jetnet_leid_t owner_leid, uint32_t seq_id, void* data, int len){
	jetnet_base_socket_t*s = jetnet_get_socket(ss, id);
	if(!s){
		jetnet_errno = ELOCATESOCKET;
		jetnet_free(data);
		return -1;
	}
	if(s->socket_r == SOCKET_R_CLIENT){
		jetnet_http_pd_t*pd = (jetnet_http_pd_t*)s->ud;
		if(!pd){
			jetnet_errno = ESOCKETSTATE;
			jetnet_free(data);
			return -1;
		}
		switch(pd->state){
			case HTTP_CLIENT_STATE_NONE:
			case HTTP_CLIENT_STATE_CONNECTING:
			case HTTP_CLIENT_STATE_CLOSE:{
				jetnet_errno = ESOCKETSTATE;
				jetnet_free(data);
				return -1;
			}
			case HTTP_CLIENT_STATE_TRANS:{
				return jetnet_tcp_send(ss, id, owner_leid, seq_id, data ,len);
			}
		}
	}else if(s->socket_r == SOCKET_R_SERVER){
		jetnet_http_pd_t*pd = (jetnet_http_pd_t*)s->ud;
		if(!pd){
			jetnet_errno = ESOCKETSTATE;
			jetnet_free(data);
			return -1;
		}
		switch(pd->state){
			case HTTP_SERVER_STATE_NONE:
			case HTTP_SERVER_STATE_CLOSE:{
				jetnet_errno = ESOCKETSTATE;
				jetnet_free(data);
				return -1;
			}
			case HTTP_SERVER_STATE_TRANS:{
				if(pd->pi->pr.should_keep_alive){
					pd->respone_time = jetnet_time_now();
					list_add(&pd->respone_link_node,&jetnet_g_http->respone_link_list);
				}
				return jetnet_tcp_send(ss, id, owner_leid, seq_id, data ,len);
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

void jetnet_http_close(jetnet_ss_t*ss, int id){
	jetnet_base_socket_t*s = jetnet_get_socket(ss, id);
	if(!s)
		return;
	jetnet_http_pd_t*pd = (jetnet_http_pd_t*)s->ud;
	if(s->socket_r == SOCKET_R_CLIENT){
		pd->state = HTTP_CLIENT_STATE_CLOSE;
	}else if(s->socket_r == SOCKET_R_SERVER){
		pd->state = HTTP_SERVER_STATE_CLOSE;
	}
	jetnet_close_socket(ss, id);
}

jetnet_proto_t* jetnet_http_create(){
	if(jetnet_g_http)
		return &jetnet_g_http->base;
	jetnet_g_http = (jetnet_net_http_t*)jetnet_malloc(sizeof(jetnet_net_http_t));
	jetnet_g_http->base.f_malloc_pi = &jetnet_http_malloc_pi;
	jetnet_g_http->base.free_pi = &jetnet_http_free_pi;
	jetnet_g_http->base.parser_reset = &jetnet_http_parser_reset;
	jetnet_g_http->base.parser = &jetnet_http_parser;
	jetnet_g_http->base.poll = &jetnet_http_poll;
	jetnet_g_http->base.ev_handler = &jetnet_http_ev_handler;
	jetnet_g_http->base.listen = &jetnet_http_listen;	
	jetnet_g_http->base.connect = &jetnet_http_connect;
	jetnet_g_http->base.send = &jetnet_http_send;
	jetnet_g_http->base.close = &jetnet_http_close;	
	INIT_LIST_HEAD(&(jetnet_g_http->respone_link_list));
	http_settings = (http_parser_settings*)jetnet_malloc(sizeof(http_parser_settings));
	jetnet_init_http_callback_setting(http_settings);
	return &jetnet_g_http->base;
}

char* get_http_real_data(jetnet_http_data_t*data){
	if(!data)
		return NULL;
	char*http_data = (char*)data + JETNET_HTTP_HEADER_FIELDS_OFFSET + data->field_num*(sizeof(jetnet_http_field_t));
	return http_data;
}

void dump_http_data(jetnet_http_data_t*data){
	char*http_data = (char*)data + JETNET_HTTP_HEADER_FIELDS_OFFSET + data->field_num*(sizeof(jetnet_http_field_t));
	char	buffer[4096];
	memset(buffer,0,sizeof(buffer));
	if(data->be_request){
		sprintf(buffer+strlen(buffer),"be_request = true\n");
	}else{
		sprintf(buffer+strlen(buffer),"be_request = false\n");
	}	
	if(data->get_message_complete){
		sprintf(buffer+strlen(buffer),"get_message = true\n");
	}else{
		sprintf(buffer+strlen(buffer),"get_message = false\n");
	}
	if(data->get_header_complete){
		sprintf(buffer+strlen(buffer),"get_header = true\n");
	}else{
		sprintf(buffer+strlen(buffer),"get_header = false\n");
	}
	if(data->should_keep_alive){
		sprintf(buffer+strlen(buffer),"should_keep_alive = true\n");
	}else{
		sprintf(buffer+strlen(buffer),"should_keep_alive = false\n");
	}
	sprintf(buffer+strlen(buffer),"http_major = %d\n",data->http_major);
	sprintf(buffer+strlen(buffer),"http_minor = %d\n",data->http_minor);
	sprintf(buffer+strlen(buffer),"method = %d\n",data->method);
	sprintf(buffer+strlen(buffer),"status_code = %d\n",data->status_code);
	sprintf(buffer+strlen(buffer),"body_len = %lu\n",data->body_len);
	
	char mask;
	int i;
	int begin_pos , end_pos;
	
	begin_pos = data->url_offset;
	end_pos = data->url_offset + data->url_len;
	mask = http_data[end_pos];
	http_data[end_pos] = 0;
	sprintf(buffer+strlen(buffer),"url = %s\n",http_data+begin_pos);
	http_data[end_pos] = mask;
	
	for(i = 0 ; i < data->field_num ; i++){			
		begin_pos = data->field_infos[i].key_offset;
		end_pos = data->field_infos[i].key_offset + data->field_infos[i].key_len;
		mask = http_data[end_pos];
		http_data[end_pos] = 0;
		sprintf(buffer+strlen(buffer),"%s",http_data+begin_pos);
		http_data[end_pos] = mask;
		
		begin_pos = data->field_infos[i].value_offset;
		end_pos = data->field_infos[i].value_offset + data->field_infos[i].value_len;
		mask = http_data[end_pos];
		http_data[end_pos] = 0;
		sprintf(buffer+strlen(buffer)," = %s\n",http_data+begin_pos);
		http_data[end_pos] = mask;
	}
	printf("%s",buffer);
}