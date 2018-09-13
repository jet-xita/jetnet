#include <string.h>

#include "jetnet_malloc.h"
#include "jetnet_ns.h"
#include "jetnet_ss.h"
#include "jetnet_proto.h"
#include "jetnet_tcppkg.h"
#include "jetnet_tcpsac.h"
#include "jetnet_http.h"

typedef struct jetnet_ns_t{
	jetnet_ss_t*ss;
	jetnet_proto_t* protos[JETNET_MAX_PROTO];
}jetnet_ns_t;

jetnet_ns_t* g_ns = NULL;

int jetnet_ns_init(){
	if(g_ns)
		return 0;
	jetnet_ss_t*ss =  jetnet_ss_create();
	if(!ss)
		return 0;	
	g_ns = (jetnet_ns_t*)jetnet_malloc(sizeof(jetnet_ns_t));
	if(!g_ns){
		jetnet_ss_release(ss);
		return 0;
	}
	int i;
	for(i = 0; i< JETNET_MAX_PROTO; i++)
		g_ns->protos[i] = NULL;
	g_ns->ss = ss;

	//init tcppkg
	jetnet_proto_t*proto_obj = jetnet_tcppkg_create();
	if(proto_obj){
		g_ns->protos[JETNET_PROTO_TCPPKG] = proto_obj;
	}

	//init tcpsac
	proto_obj = jetnet_tcpsac_create();
	if(proto_obj){
		g_ns->protos[JETNET_PROTO_TCPSAC] = proto_obj;
	}
	//init http
	proto_obj = jetnet_http_create();
	if(proto_obj){
		g_ns->protos[JETNET_PROTO_HTTP] = proto_obj;
	}
	return 1;
}


void jetnet_ns_release(){
	if(!g_ns)
		return;
	jetnet_ss_release(g_ns->ss);
	
	//release all protocol
	jetnet_tcppkg_release();
	jetnet_tcpsac_release();
	jetnet_http_release();
	//clear protocol data
	memset(g_ns->protos,0,sizeof(g_ns->protos));	
	jetnet_free(g_ns);
	g_ns = NULL;
}

int jetnet_net_listen(jetnet_leid_t owner_leid, int protocol, const char * host, int port, int backlog){
	if(!g_ns)
		return -1;
	if(protocol < 0 || protocol >= JETNET_MAX_PROTO)
		return -1;	
	if(!g_ns->protos[protocol])
		return -1;
	return g_ns->protos[protocol]->listen(g_ns->ss,owner_leid,host,port,backlog);
}

int jetnet_net_connect(jetnet_leid_t owner_leid, int protocol, const char * host, int port){
	if(!g_ns)
		return -1;
	if(protocol < 0 || protocol >= JETNET_MAX_PROTO)
		return -1;
	if(!g_ns->protos[protocol])
		return -1;
	return g_ns->protos[protocol]->connect(g_ns->ss,owner_leid,host,port);
}

int jetnet_net_send(int id, jetnet_leid_t owner_leid, uint32_t seq_id, void* data, int len){
	if(!g_ns)
		return -1;
	jetnet_base_socket_t*s = jetnet_get_socket(g_ns->ss, id);
	if(!s)
		return -1;
	jetnet_base_pd*base_pd = (jetnet_base_pd*)s->ud;
	if(!base_pd)
		return -1;	
	if(!g_ns->protos[base_pd->protocol])
		return -1;
	return g_ns->protos[base_pd->protocol]->send(g_ns->ss,id,owner_leid,seq_id,data,len);
}

int jetnet_net_close(int id){
	if(!g_ns)
		return -1;
	jetnet_base_socket_t*s = jetnet_get_socket(g_ns->ss, id);
	if(!s)
		return -1;
	jetnet_base_pd*base_pd = (jetnet_base_pd*)s->ud;
	if(!base_pd)
		return -1;
	if(!g_ns->protos[base_pd->protocol])
		return -1;
	g_ns->protos[base_pd->protocol]->close(g_ns->ss,id);
	return 0;
}

int jetnet_net_is_connected(int id){
	if(!g_ns)
		return 0;
	return jetnet_tcp_is_connected(g_ns->ss,id);
}

int jetnet_net_poll(jetnet_mq_t*mq, int timeout){
	if(!g_ns)
		return -1;
	jetnet_pe_t ev;
	if( jetnet_ss_poll(g_ns->ss, &ev, timeout)){
		if(ev.ev_type != POLL_EV_NONE && ev.s){
			if(ev.ev_type == POLL_EV_ACCEPT){
				jetnet_base_socket_t*parent_s = jetnet_get_socket(g_ns->ss, ev.s->parent_id);
				if(parent_s){
					jetnet_base_pd*base_pd = (jetnet_base_pd*)parent_s->ud;
					if(base_pd && g_ns->protos[base_pd->protocol]){
						g_ns->protos[base_pd->protocol]->ev_handler(g_ns->ss,&ev,mq);
					}
				}
			}else{
				jetnet_base_pd*base_pd = (jetnet_base_pd*)ev.s->ud;
				if(base_pd && g_ns->protos[base_pd->protocol]){
					g_ns->protos[base_pd->protocol]->ev_handler(g_ns->ss,&ev,mq);
				}
			}
		}
	}
	int i;
	for(i = 0 ; i < JETNET_MAX_PROTO; i++){
		if(g_ns->protos[i])
			g_ns->protos[i]->poll(g_ns->ss,mq);	
	}
	return 0;
}

jetnet_ss_t* jetnet_get_ss(){
	return g_ns->ss;
}