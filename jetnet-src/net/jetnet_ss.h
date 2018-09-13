#ifndef JETNET_SS_H_INCLUDED
#define JETNET_SS_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include "jetnet_iobuf.h"
#include "jetnet_const.h"
#include "jetnet_msg.h"

#if defined(__cplusplus)
extern "C" {
#endif

struct jetnet_ss_t;
typedef struct jetnet_ss_t jetnet_ss_t;

//define the ud data free function type
typedef void (*jetnet_sf_ud_free)(void*ud);

//define the socket layer protocol
enum{
	SOCKET_P_UNKNOW = 0,
	SOCKET_P_TCP = 1,
	SOCKET_P_UDP = 2,
};

//define the socket role type
enum{
	SOCKET_R_UNKNOW = 0,
	SOCKET_R_LISTENER = 1,
	SOCKET_R_CLIENT = 2,
	SOCKET_R_SERVER = 4,
};

//define the IP version flag
enum{
	FAMILY_UNKNOW = 0,
	FAMILY_IPV4 = 1,
	FAMILY_IPV6 = 2,
};

//define the poll event type
enum{
	POLL_EV_NONE = 0,
	POLL_EV_CONNECT,
	POLL_EV_RECV,
	POLL_EV_ACCEPT,
	POLL_EV_CLOSE,
};

//define the write buffer list for socket	
typedef struct jetnet_swb_t{
	jetnet_wb_t base;
	//helper info
	uint32_t seq_id;
	jetnet_leid_t owner_leid;
	//address for udp,tcp's write buffer hasn't this field
	char udp_address[ADDRESS_SIZE];
	int udp_port;
}jetnet_swb_t;

//define the base socket data struct
typedef struct jetnet_base_socket_t{
	int id;
	int parent_id;
	jetnet_leid_t owner_leid;
	void* ud;
	jetnet_sf_ud_free free_func;
	int socket_p;
	int socket_r;
	jetnet_wbl_t wbl; //write buffer list,we should not operate this wbl manual except push data to it
	jetnet_rbl_t rbl; //read buffer list,we should not operate this rbl manual except read data from it
	//peer address info
	char peer_host[ADDRESS_SIZE];
	int peer_port;
	//state information
	unsigned long establish_time;
	//static information
	uint64_t total_recv_size;
	uint64_t total_recv_count;
	uint64_t total_send_size;
	uint64_t total_send_count;
}jetnet_base_socket_t;

typedef struct jetnet_pe_connect_t{
	int result; //0 fail 1 ok
	int errcode;
}jetnet_pe_connect_t;

typedef struct jetnet_pe_recv_t{
	jetnet_rb_t* rb;
}jetnet_pe_recv_t;

typedef struct jetnet_pe_close_t{
	int errcode;
}jetnet_pe_close_t;

typedef struct jetnet_pe_t{
	int ev_type;
	jetnet_base_socket_t*s;
	union{
		jetnet_pe_connect_t connect;
		jetnet_pe_recv_t recv;
		jetnet_pe_close_t close;
	}data;
}jetnet_pe_t;

//common operator
jetnet_ss_t * jetnet_ss_create();

void jetnet_ss_release(jetnet_ss_t *ss);

jetnet_base_socket_t* jetnet_get_socket(jetnet_ss_t*ss, int id);

void jetnet_close_socket(jetnet_ss_t*ss, int id);

/*
return the socket id , -1 while occurse error.
*/
int jetnet_tcp_listen(jetnet_ss_t*ss, jetnet_leid_t owner_leid, const char * host, int port, int backlog);

/*
return the socket id , -1 while occurse error.
*/
int jetnet_tcp_connect(jetnet_ss_t*ss, jetnet_leid_t owner_leid, const char * host, int port);

/*
return 1 if the socket is connected otherwise return 0
we should call this function after we call jetnet_tcp_connect to check
the socket whether connected succeed immediate.
*/
int jetnet_tcp_is_connected(jetnet_ss_t*ss, int id);

/*
return 0 if succeed , -1 while occurse error.
*/
int jetnet_tcp_send(jetnet_ss_t*ss, int id, jetnet_leid_t owner_leid, uint32_t seq_id , void* data ,int len);

/*
return 0 if succeed , -1 while occurse error.
*/
int jetnet_tcp_sendv(jetnet_ss_t*ss, int id, jetnet_leid_t owner_leid, uint32_t seq_id, jetnet_iov_t*vec ,int count);

/*
return the socket id , -1 while occurse error.
*/
int jetnet_udp_create(jetnet_ss_t*ss, jetnet_leid_t owner_leid, int family);

/*
return 0 when succeed , -1 while occurse error.
*/
int jetnet_udp_bind(jetnet_ss_t*ss, int id, const char * host, int port);

/*
return 0 when succeed , -1 while occurse error.
*/
int jetnet_udp_connect(jetnet_ss_t*ss, int id, const char * host, int port);

/*
return 0 if succeed , -1 while occurse error.
*/
int jetnet_udp_send(jetnet_ss_t*ss, int id, jetnet_leid_t owner_leid, uint32_t seq_id, void* data ,int len , const char * host, int port);

/*
poll,return 1 when get task to handle or 0 when idle,and -1 while occurse error.
*/
int jetnet_ss_poll(jetnet_ss_t*ss, jetnet_pe_t*ev, int timeout);


#if defined(__cplusplus)
}
#endif

#endif /* JETNET_SS_H_INCLUDED */
