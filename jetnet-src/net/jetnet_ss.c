#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h> 
#include <arpa/inet.h>  
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

#include "jetnet_ss.h"
#include "jetnet_epoll.h"
#include "jetnet_malloc.h"
#include "jetnet_errno.h"
#include "jetnet_macro.h"
#include "jetnet_time.h"
#include "list.h"

#define MAX_SOCKET_BIT 16
#define MAX_EVENT 64
#define MAX_SOCKET (1<<MAX_SOCKET_BIT)
#define HASH_ID(id) (((unsigned)id) % MAX_SOCKET)
#define MAX_UDP_PACKAGE 65535

#define IOVEC_NUM_PER_SEND 16

// EAGAIN and EWOULDBLOCK may be not the same value.
#if (EAGAIN != EWOULDBLOCK)
#define AGAIN_WOULDBLOCK EAGAIN : case EWOULDBLOCK
#else
#define AGAIN_WOULDBLOCK EAGAIN
#endif

#define SIZEOF_TCPBUFFER (offsetof(jetnet_swb_t, udp_address[0]))
#define SIZEOF_UDPBUFFER (sizeof(jetnet_swb_t))

#define FREE_IOVEC(_vec,_count)\
	do{\
		int _free_counter;\
		for(_free_counter = 0 ; _free_counter < _count; _free_counter++)\
			jetnet_free((_vec)[_free_counter].iov_base);\
	}while(0)

#define MIN_RECV_SIZE	 256
#define MAX_RECV_SIZE	 4096

//will chage the input value ,so ,can't input an point or refrence
#define GET_RECV_SIZE(current_size,active_size) (\
        (active_size)>=(current_size) ? MIN((current_size)<<1,MAX_RECV_SIZE) : (\
		(active_size)<<1 < (current_size) ? MAX(MIN_RECV_SIZE,(current_size)>>1) : (current_size)\
		))

//define socket state
enum{
	SOCKET_STATE_INVALID = 0, //this socket object had free
	SOCKET_STATE_CONNECTING, //connecting to server ,wait for connection establish
	SOCKET_STATE_CLOSING, //the socket is close the the app,but there are some data wait for sent.
	SOCKET_STATE_CONNECTED, //the socket in normal connected state
	SOCKET_STATE_LISTENING,	//the socket is the listening socket,and it do listing now.
	SOCKET_STATE_ERROR,	//the socket is in an error state,wait for app to close(we give the app an last chance to get the wbl/rbl before close)
};

//net config field
int LINGER_TIME = 5;
int SEND_BUFF_SIZE = 16384; //16k
int RECV_BUFF_SIZE = 16384; //16k

bool USE_LINGER = false; //是否使用linger算法
bool NON_BLOCK = true; //是否使用非阻塞socket
bool ADDR_REUSE = true; //侦听地址是否复用
bool KEEP_ALIVE = false; //是否保活
bool USE_NAGLE = true; //是否禁用nagle算法
bool SET_SEND_BUFFER_SIZE = false; //是否设置发送缓冲区大小
bool SET_RECV_BUFFER_SIZE = false; //是否设置接收缓冲区大小

typedef struct jetnet_socket_t{
	jetnet_base_socket_t base; // the base struct data
	int fd; // socket fd
	unsigned int active_token;
	int family; //indicate the ip family	
	uint8_t	socket_state; // the socket layer state
	bool can_send; //indicate whether this socket can send data
	bool can_recv; //indicate whether this socket had data cache in buffer for recv
	struct list_head active_node; //active link node
	struct list_head close_node; // close link node
	int recv_size; //next recv buffer size
	int cache_errno;
	char udp_address[ADDRESS_SIZE]; //the udp connect host
	int udp_port; //the udp connect host
}jetnet_socket_t;

struct jetnet_ss_t{
	unsigned int active_token;
	int event_fd; //epoll fd
	int alloc_id; //the next socket object id
	struct list_head active_list;
	struct list_head close_list;
	jetnet_socket_t slot[MAX_SOCKET]; //the socket objects
	uint8_t udp_buffer[MAX_UDP_PACKAGE]; //temp buffer for udp recv
};

//check where if the ip host is ipv6 format
bool jetnet_is_ipv6(const char* ip){
    const char*p = ip;
    int cnt = 0;
    for(; *p != '\0';p++){
        if(*p == ':' && (++cnt>1) ){
			return true;
		}
	}
	return false;
}

//socket operation helper function
int jetnet_socket_reuseaddr(int fd,int flags){
	return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flags, sizeof(flags));
}

int jetnet_socket_noblock(int fd,int flags){
	int ret = fcntl(fd, F_GETFL);
	if(ret < 0)
		return -1;
	if(flags == 1){
		ret |= O_NONBLOCK;
	}else{
		ret &= ~O_NONBLOCK;
	}
	if(fcntl(fd, F_SETFL, ret)<0)
		return -1;
	return 0;
}

int jetnet_socket_linger(int fd,int time){	
	struct linger linger_val;
	if(time < 0){
		linger_val.l_onoff = 0;
		linger_val.l_linger = 0;
	}else{
		linger_val.l_onoff = 1;
		linger_val.l_linger = time;		
	}
	if( setsockopt(fd, SOL_SOCKET, SO_LINGER, &linger_val, sizeof(linger_val)) < 0)
		return -1;
	return 0;
}

int jetnet_socket_nagle(int fd,int flags){	
	flags = flags == 0 ? 1 : 0;
	if(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flags, sizeof(flags))<0)
		return -1;
	return 0;
}

int jetnet_socket_recvbuf_size(int fd,int size){
	if(setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (const char*)&size,sizeof(size))<0)
		return -1;
	return 0;
}

int jetnet_socket_sendbuf_size(int fd,int size){
	if(setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (const char*)&size,sizeof(size))<0)
		return -1;
	return 0;
}

int jetnet_keep_alive(int fd,int flags){
	if(setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &flags, sizeof(flags))<0)
		return -1;
	return 0;
}

//helper function for active link
static inline int	jetnet_is_active(jetnet_socket_t *s){
	if(list_empty(&s->active_node))
		return 0;
	return 1;
}

static inline void	jetnet_deactive(jetnet_socket_t *s){
	if(!list_empty(&s->active_node))
		list_del_init(&s->active_node);
}

static inline void	jetnet_active(jetnet_ss_t*ss,jetnet_socket_t *s){
	if(list_empty(&s->active_node)){
		list_add_tail(&s->active_node,&ss->active_list);
		s->active_token = ss->active_token;
	}
}

static inline int jetnet_has_close_flag(jetnet_socket_t *s){
	if(list_empty(&s->close_node))
		return 0;
	return 1;
}

static inline void	jetnet_clean_close_flag(jetnet_socket_t *s){
	if(!list_empty(&s->close_node))
		list_del_init(&s->close_node);
}

static inline void	jetnet_set_close_flag(jetnet_ss_t*ss,jetnet_socket_t *s){
	if(list_empty(&s->close_node))
		list_add_tail(&s->close_node,&ss->close_list);
}

// just reset data , but don't free the resource.
void jetnet_reset_socket(jetnet_socket_t *s) {
	if(!s)
		return;
	s->base.id = -1;
	s->base.parent_id = -1;
	s->base.ud = NULL;
	s->base.free_func = NULL;
	s->base.socket_p = SOCKET_P_UNKNOW;
	s->base.socket_r = SOCKET_R_UNKNOW;
	jetnet_wbl_clear(&s->base.wbl);
	jetnet_rbl_clear(&s->base.rbl);
	s->base.establish_time = 0;
	s->base.total_recv_size = 0;
	s->base.total_recv_count = 0;
	s->base.total_send_size = 0;
	s->base.total_send_count = 0;
	
	s->fd = -1;
	s->socket_state = SOCKET_STATE_INVALID;
	s->can_send = false;
	s->can_recv = false;
	s->family = FAMILY_UNKNOW;
	s->active_token = 0;
	INIT_LIST_HEAD(&s->active_node);
	INIT_LIST_HEAD(&s->close_node);	
	s->recv_size = MIN_RECV_SIZE;
	s->cache_errno = 0;
	memset(s->udp_address,0,sizeof(s->udp_address));
	s->udp_port = 0;
}

jetnet_socket_t * jetnet_alloc_socket(jetnet_ss_t *ss) {
	int id;
	int i;
	for (i=0;i<MAX_SOCKET;i++) {
		id = ss->alloc_id++;
		if (id < 0)
			id = 0;
		jetnet_socket_t *s = &ss->slot[HASH_ID(id)];
		if (s->socket_state == SOCKET_STATE_INVALID){
			s->base.id = id;
			return s;
		}
	}
	return NULL;
}

void jetnet_free_socket_resource(jetnet_ss_t *ss, jetnet_socket_t *s) {
	//free the resource
	jetnet_wbl_free(&s->base.wbl);
	jetnet_rbl_free(&s->base.rbl);
	if(s->fd != -1){
		jetnet_epoll_del(ss->event_fd, s->fd);
		close(s->fd);
		s->fd = -1;
	}
	if(s->base.ud){
		if(s->base.free_func){
			s->base.free_func(s->base.ud);
		}else{
			jetnet_free(s->base.ud);
		}		
	}
	s->base.free_func = NULL;
	s->base.ud = NULL;
}

void jetnet_force_close(jetnet_ss_t *ss, jetnet_socket_t *s) {
	if (s->socket_state == SOCKET_STATE_INVALID)
		return;
	//remove from active link
	jetnet_deactive(s);	
	//remove from delete link
	jetnet_clean_close_flag(s);
	//free resource
	jetnet_free_socket_resource(ss,s);
	//reset member
	jetnet_reset_socket(s);
}

jetnet_ss_t * jetnet_ss_create() {
	int i;
	int efd = jetnet_epoll_create();
	if (-1 == efd)
		return NULL;
	jetnet_ss_t *ss = (jetnet_ss_t *)jetnet_malloc(sizeof(jetnet_ss_t));
	ss->event_fd = efd;
	ss->alloc_id = 0;
	ss->active_token = 0;
	INIT_LIST_HEAD(&ss->active_list);
	INIT_LIST_HEAD(&ss->close_list);
	memset(ss->udp_buffer,0,sizeof(ss->udp_buffer));
	for (i=0;i<MAX_SOCKET;i++) {
		jetnet_socket_t *s = &ss->slot[i];
		jetnet_reset_socket(s);
	}
	return ss;
}

void jetnet_ss_release(jetnet_ss_t *ss) {
	if(!ss)
		return;
	int i;
	for (i=0;i<MAX_SOCKET;i++) {
		jetnet_socket_t *s = &ss->slot[i];
		if (s->socket_state != SOCKET_STATE_INVALID)
			jetnet_free_socket_resource(ss,s);
	}
	jetnet_epoll_release(ss->event_fd);
	jetnet_free(ss);
}

jetnet_base_socket_t* jetnet_get_socket(jetnet_ss_t*ss, int id){
	if(id < 0 )
		return NULL;
	jetnet_socket_t *s = &ss->slot[HASH_ID(id)];
	if(s->socket_state == SOCKET_STATE_INVALID)
		return NULL;
	return &s->base;
}

jetnet_socket_t* jetnet_get_socket_org(jetnet_ss_t*ss, int id){
	if(id < 0 )
		return NULL;
	jetnet_socket_t *s = &ss->slot[HASH_ID(id)];
	if(s->socket_state == SOCKET_STATE_INVALID)
		return NULL;
	return s;
}

void jetnet_close_socket(jetnet_ss_t*ss, int id){
	if(id < 0 )
		return;
	jetnet_socket_t *s = &ss->slot[HASH_ID(id)];
	if(s->socket_state == SOCKET_STATE_INVALID)
		return;
	if(s->socket_state == SOCKET_STATE_CONNECTED){
		if(jetnet_wbl_is_empty(&s->base.wbl)){
			jetnet_force_close(ss,s);
			return;
		}
		s->socket_state = SOCKET_STATE_CLOSING;
	}else{
		jetnet_force_close(ss,s);
	}
}


/*
return the socket id , -1 while occurse error.
*/
int jetnet_tcp_listen(jetnet_ss_t*ss, jetnet_leid_t owner_leid, const char * host, int port, int backlog){
	//alloc an empty socket object
	jetnet_socket_t *s = jetnet_alloc_socket(ss);
	if(!s){
		jetnet_errno = EALLOCSOCKET;
		return -1;
	}
	if(!host || host[0] == '\0')
		host = "0.0.0.0";
	int family = FAMILY_UNKNOW;
	if(jetnet_is_ipv6(host)){
		family = FAMILY_IPV6;
	}else{
		family = FAMILY_IPV4;
	}	
	//new a socket handle and do listen work
    int s_fd = socket(family == FAMILY_IPV6 ? AF_INET6 : AF_INET,SOCK_STREAM, 0);
    if (s_fd < 0){
		jetnet_errno = errno;
		return -1;
	}
	//set up the socket options
	if(jetnet_socket_reuseaddr(s_fd,1) < 0){
		close(s_fd);
		jetnet_errno = errno;
		return -1;
	}
	if(jetnet_socket_noblock(s_fd,1) < 0){
		close(s_fd);
		jetnet_errno = errno;
		return -1;
	}
	int ret;
	if( family == FAMILY_IPV6 ){
		struct sockaddr_in6 addr = { 0 };
		addr.sin6_family = AF_INET6;
		inet_pton(AF_INET6, host, &addr.sin6_addr);
		addr.sin6_port  = htons(port);
		ret = bind(s_fd, (struct sockaddr*)(&addr), sizeof(struct sockaddr_in6));
	}else{
		struct sockaddr_in addr = { 0 };
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = inet_addr(host);
		addr.sin_port = htons(port);
		ret = bind(s_fd, (struct sockaddr*)(&addr), sizeof(struct sockaddr_in));
	}	
    if (ret < 0){
		jetnet_errno = errno;
        close(s_fd);
        return -1;
    }
	ret = listen(s_fd, backlog);
    if (ret < 0){
		jetnet_errno = errno;
        close(s_fd);
        return -1;
    }
	//associate the socket to the epoll
	if(jetnet_epoll_add(ss->event_fd, s_fd, EPOLLIN|EPOLLRDHUP|EPOLLET, s) != 0){
		jetnet_errno = errno;
		close(s_fd);
		return -1;
	}
	//set up
	s->base.parent_id = -1;
	s->base.owner_leid = owner_leid;
	s->base.socket_p = SOCKET_P_TCP;
	s->base.socket_r = SOCKET_R_LISTENER;
	s->fd = s_fd;
	s->socket_state = SOCKET_STATE_LISTENING;
	s->can_send = false;
	s->can_recv = false;
	INIT_LIST_HEAD(&s->active_node);
	s->family = family;	
	return s->base.id;
}

/*
return the socket id , -1 while occurse error.
*/
int jetnet_tcp_connect(jetnet_ss_t*ss, jetnet_leid_t owner_leid, const char * host, int port){
	if(host==NULL || host[0] == '\0'){
		jetnet_errno = EPARAMINVALID;
		return -1;
	}
	jetnet_socket_t *s = jetnet_alloc_socket(ss);
	if(!s){
		jetnet_errno = EALLOCSOCKET;
		return -1;
	}
	
	int family = FAMILY_UNKNOW;
	if(jetnet_is_ipv6(host)){
		family = FAMILY_IPV6;
	}else{
		family = FAMILY_IPV4;
	}	
    int s_fd = socket(family == FAMILY_IPV6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
    if (s_fd < 0){
		jetnet_errno = errno;
		return -1;
	}
	//set up socket options
	if(jetnet_socket_noblock(s_fd,1) < 0){
		close(s_fd);
		jetnet_errno = errno;
		return -1;
	}
	if(jetnet_socket_linger(s_fd,3) < 0){
		close(s_fd);
		jetnet_errno = errno;
		return -1;
	}
	if(jetnet_socket_nagle(s_fd,0) < 0){
		close(s_fd);
		jetnet_errno = errno;
		return -1;
	}
	if(jetnet_keep_alive(s_fd,1) < 0){
		close(s_fd);
		jetnet_errno = errno;
		return -1;
	}
	//start connect
	int ret;
	do{
		if( family == FAMILY_IPV6 ){
			struct sockaddr_in6 addr = { 0 };
			addr.sin6_family = AF_INET6;
			inet_pton(AF_INET6, host, &addr.sin6_addr);		
			addr.sin6_port  = htons(port);
			ret = connect(s_fd, (struct sockaddr*)(&addr), sizeof(struct sockaddr_in6));
		}else{
			struct sockaddr_in addr = { 0 };
			addr.sin_family = AF_INET;
			addr.sin_addr.s_addr = inet_addr(host);
			addr.sin_port = htons(port);
			ret = connect(s_fd, (struct sockaddr*)(&addr), sizeof(struct sockaddr_in));
		}
	}while(ret < 0 && errno == EINTR);
	
    if (ret < 0 && errno != EINPROGRESS){
		close(s_fd);
		jetnet_errno = errno;
		return -1;
    }
	//associate the socket to the epoll
	if(jetnet_epoll_add(ss->event_fd, s_fd, EPOLLIN|EPOLLOUT|EPOLLRDHUP|EPOLLET, s) != 0){
		close(s_fd);
		jetnet_errno = errno;
		return -1;
	}
	//set up
	s->base.parent_id = -1;
	s->base.owner_leid = owner_leid;
	s->base.socket_p = SOCKET_P_TCP;
	s->base.socket_r = SOCKET_R_CLIENT;
	s->fd = s_fd;
	s->family = family;
	INIT_LIST_HEAD(&s->active_node);
	
	int copy_len = MIN(strlen(host),ADDRESS_SIZE-1);
	strncpy(s->base.peer_host,host,copy_len);
	s->base.peer_host[copy_len] = 0;
	s->base.peer_port = port;
	
	if(ret == 0) {
		s->base.establish_time = jetnet_time_now();
		s->socket_state = SOCKET_STATE_CONNECTED;
		s->can_send = true;
		s->can_recv = false;
		return s->base.id;
	}
	s->socket_state = SOCKET_STATE_CONNECTING;
	s->can_send = false;
	s->can_recv = false;
	return s->base.id;
}

int jetnet_tcp_is_connected(jetnet_ss_t*ss, int id){
	jetnet_socket_t*s = jetnet_get_socket_org(ss,id);
	if(!s)
		return 0;
	if(s->socket_state == SOCKET_STATE_CONNECTED)
		return 1;
	return 0;
}

// 0:no error,no data need to be send.
// 1:no error,socket's buffer is not full
// 2:no error,socket's buffer is full
// -1:get socket error
int jetnet_send_buffer_list(jetnet_socket_t*s,int*err,int*send_cnt){
	*err = 0;
	*send_cnt = 0;
	//calculate the data vec for send
	struct iovec vec[IOVEC_NUM_PER_SEND];	
	int total_size = 0;
	int count = jetnet_wbl_get_iovec(&s->base.wbl,(jetnet_iov_t*)vec, IOVEC_NUM_PER_SEND, &total_size);
	if(count == 0)
		return 0;	
	//begin to send
	int send_ret;
	do{
		if( count == 1)
			send_ret = send(s->fd, vec[0].iov_base, vec[0].iov_len, 0);
		else
			send_ret = writev(s->fd, vec, count);
	}while(send_ret < 0 && errno == EINTR);

	if (send_ret < 0){
		if(errno == EAGAIN || errno == EWOULDBLOCK){
			return 2;
		}else{
			// socket error
			*err = errno;
			return -1;
		}
	}else{
		//update the static information
		s->base.total_send_size += send_ret;
		s->base.total_send_count++;
		//handle the send buffer list
		*send_cnt = send_ret;
		jetnet_wbl_drop(&s->base.wbl,send_ret);
		//check to return value
		if(send_ret < total_size){
			return 2;
		}else{
			return 1;
		}
	}
}

// 1:no error,socket's buffer is not full
// 2:no error,socket's buffer is full
// -1:get socket error
int jetnet_direct_send(jetnet_socket_t*s, char*buffer, int size, int*err, int*send_cnt){
	*err = 0;
	*send_cnt = 0;
	//begin to send
	int send_ret;
	do{
		send_ret = send(s->fd, buffer, size, 0);
	}while(send_ret < 0 && errno == EINTR);

	if (send_ret < 0){
		if(errno == EAGAIN || errno == EWOULDBLOCK){
			return 2;
		}else{
			// socket error
			*err = errno;
			return -1;
		}
	}else{
		//update the static information
		s->base.total_send_size += send_ret;
		s->base.total_send_count++;		
		//handle the send buffer list
		*send_cnt = send_ret;
		if(send_ret == size){
			return 1;			
		}else{
			return 2;
		}
	}
}

// 1:no error,socket's buffer is not full
// 2:no error,socket's buffer is full
// -1:get socket error
int jetnet_direct_sendv(jetnet_socket_t*s, struct iovec*vec, int count, int*err, int*send_cnt){
	*err = 0;
	*send_cnt = 0;
	//begin to send
	int send_ret;
	do{
		send_ret = writev(s->fd, vec, count);
	}while(send_ret < 0 && errno == EINTR);

	if (send_ret < 0){
		if(errno == EAGAIN || errno == EWOULDBLOCK){
			return 2;
		}else{
			// socket error
			*err = errno;
			return -1;
		}
	}else{
		//update the static information
		s->base.total_send_size += send_ret;
		s->base.total_send_count++;
		//handle the send buffer list
		*send_cnt = send_ret;
		int i;
		int total = 0;
		for(i=0; i<count; i++){
			total += vec[i].iov_len;
		}
		if(send_ret == total){
			return 1;			
		}else{
			return 2;
		}
	}
}

/*
return 0 if succeed , -1 while occurse error.
*/
int jetnet_tcp_send(jetnet_ss_t*ss, int id, jetnet_leid_t owner_leid, uint32_t seq_id , void* data ,int len){
	if(data == NULL || len <= 0){
		jetnet_errno = EPARAMINVALID;
		return -1;
	}
	jetnet_socket_t*s = jetnet_get_socket_org(ss,id);
	if(!s){
		jetnet_free(data);
		jetnet_errno = ELOCATESOCKET;
		return -1;
	}
	// check whether the socket is tcp socket
	if(s->base.socket_p != SOCKET_P_TCP){
		jetnet_free(data);
		jetnet_errno = EPROTOCOL;
		return -1;
	}
	// if the socket is listener , can't send data
	if(s->base.socket_r != SOCKET_R_CLIENT && s->base.socket_r != SOCKET_R_SERVER){
		jetnet_free(data);
		jetnet_errno = ESOCKETROLE;
		return -1;
	}
	// if the socket in error state,we can send data event the socket is connecting
	if(s->socket_state != SOCKET_STATE_CONNECTED && s->socket_state != SOCKET_STATE_CONNECTING){
		jetnet_free(data);
		jetnet_errno = ESOCKETSTATE;
		return -1;
	}
	// if the socket is block to send now , just push the buffer to the write buffer list
	if(!s->can_send){
		//append the data to wbl
		jetnet_swb_t*wb = (jetnet_swb_t *)jetnet_malloc(SIZEOF_TCPBUFFER);
		wb->owner_leid = owner_leid;
		wb->seq_id = seq_id;
		wb->base.vec[0].iov_base = data;
		wb->base.vec[0].iov_len = len;
		wb->base.vec_count = 1;
		wb->base.offset = 0;
		jetnet_wbl_appen(&s->base.wbl, &wb->base);
		//return
		return 0;
	}else{
		int send_err = 0;
		int send_cnt = 0;
		int send_result = 0;
		
		if(jetnet_wbl_is_empty(&s->base.wbl)){
			//direct send
			send_result = jetnet_direct_send(s, data, len, &send_err, &send_cnt);			
			switch(send_result){
				case 1:{// 完全发送成功
					jetnet_free(data);
					return 0;
				}
				case 2:{// 部分发送
					//set up the block flag
					s->can_send = false;
					//push to wbl
					jetnet_swb_t*wb = (jetnet_swb_t *)jetnet_malloc(SIZEOF_TCPBUFFER);
					wb->owner_leid = owner_leid;
					wb->seq_id = seq_id;
					wb->base.vec[0].iov_base = data;
					wb->base.vec[0].iov_len = len;
					wb->base.vec_count = 1;
					wb->base.offset = send_cnt;
					jetnet_wbl_appen(&s->base.wbl, &wb->base);
					return 0;
				}
				case -1:{// 错误
					//free buffer
					jetnet_free(data);
					s->cache_errno = send_err;
					s->socket_state = SOCKET_STATE_ERROR;
					//now we should add the socket to active link,we will report socket close event in socket poll
					jetnet_active(ss,s);
					//set up the error code
					jetnet_errno = send_err;
					return -1;
				}
			}
		}else{
			//push to wbl,this situation,means the socket object has been in active links.
			jetnet_swb_t*wb = (jetnet_swb_t *)jetnet_malloc(SIZEOF_TCPBUFFER);
			wb->owner_leid = owner_leid;
			wb->seq_id = seq_id;
			wb->base.vec[0].iov_base = data;
			wb->base.vec[0].iov_len = len;
			wb->base.vec_count = 1;
			wb->base.offset = 0;
			jetnet_wbl_appen(&s->base.wbl, &wb->base);
			return 0;
		}
	}
	return -1;
}

/*
return 0 if succeed , -1 while occurse error.
*/
int jetnet_tcp_sendv_(jetnet_ss_t*ss, int id, jetnet_leid_t owner_leid, uint32_t seq_id, jetnet_iov_t*vec ,int count){
	jetnet_socket_t*s = jetnet_get_socket_org(ss,id);
	if(!s){
		FREE_IOVEC(vec,count);
		jetnet_errno = ELOCATESOCKET;
		return -1;
	}
	// check whether the socket is tcp socket
	if(s->base.socket_p != SOCKET_P_TCP){
		FREE_IOVEC(vec,count);
		jetnet_errno = EPROTOCOL;
		return -1;
	}
	// if the socket is listener , can't send data
	if(s->base.socket_r != SOCKET_R_CLIENT && s->base.socket_r != SOCKET_R_SERVER){
		FREE_IOVEC(vec,count);
		jetnet_errno = ESOCKETROLE;
		return -1;
	}
	// if the socket isn't in connected state
	if(s->socket_state != SOCKET_STATE_CONNECTED && s->socket_state != SOCKET_STATE_CONNECTING){
		FREE_IOVEC(vec,count);
		jetnet_errno = ESOCKETSTATE;
		return -1;
	}
	// if the socket is block to send now , just push the buffer to the write buffer list
	if(!s->can_send){
		//append the data to wbl
		jetnet_swb_t*wb = (jetnet_swb_t *)jetnet_malloc(SIZEOF_TCPBUFFER);
		wb->owner_leid = owner_leid;		
		wb->seq_id = seq_id;
		memcpy(wb->base.vec,vec,sizeof(jetnet_iov_t)*count);
		wb->base.vec_count = count;		
		wb->base.offset = 0;	
		jetnet_wbl_appen(&s->base.wbl, &wb->base);		
		return 0;
	}
	else{
		//now the wbl must be empty
		if(jetnet_wbl_is_empty(&s->base.wbl)){
			//try to direct send data
			int send_cnt = 0;
			int send_err = 0;
			int send_result = 0;
			
			// 1:no error,socket's buffer is not full
			// 2:no error,socket's buffer is full
			// -1:get socket error
			send_result = jetnet_direct_sendv(s,(struct iovec*)vec,count,&send_cnt,&send_err);			
			switch(send_result){
				case 1:{// 完全发送成功
					FREE_IOVEC(vec,count);
					return 0;
				}
				case 2:{// 部分发送
					//append the data to wbl
					jetnet_swb_t*wb = (jetnet_swb_t *)jetnet_malloc(SIZEOF_TCPBUFFER);
					wb->owner_leid = owner_leid;
					wb->seq_id = seq_id;
					memcpy(wb->base.vec,vec,sizeof(jetnet_iov_t)*count);
					wb->base.vec_count = count;
					wb->base.offset = send_cnt;
					jetnet_wbl_appen(&s->base.wbl, &wb->base);
					//set up the block flag
					s->can_send = false;
					return 0;
				}
				case -1:{// 错误
					FREE_IOVEC(vec,count);
					s->cache_errno = send_err;
					s->socket_state = SOCKET_STATE_ERROR;
					//now we should add the socket to active link,we will report socket close event in socket poll
					jetnet_active(ss,s);
					//set up the error code				
					jetnet_errno = send_err;
					return -1;
				}
			}
		}else{
			//push to wbl
			jetnet_swb_t*wb = (jetnet_swb_t *)jetnet_malloc(SIZEOF_TCPBUFFER);
			wb->owner_leid = owner_leid;
			wb->seq_id = seq_id;
			memcpy(wb->base.vec,vec,sizeof(struct iovec)*count);
			wb->base.vec_count = count;
			wb->base.offset = 0;
			jetnet_wbl_appen(&s->base.wbl, &wb->base);
			return 0;
		}
	}
	return -1;	
}

/*
return 0 if succeed , -1 while occurse error.
*/
int jetnet_tcp_sendv(jetnet_ss_t*ss, int id, jetnet_leid_t owner_leid, uint32_t seq_id, jetnet_iov_t*vec ,int count){
	if(vec == NULL || (count <= 0 || count > JETNET_MAX_IOVEC)){
		FREE_IOVEC(vec,count);
		jetnet_errno = EPARAMINVALID;
		return -1;
	}
	int i;
	int ret = 0;
	for(i = 0 ; i < count; i++){
		if(vec[i].iov_base == NULL || vec[i].iov_len <= 0){
			ret = 1;
			break;
		}
	}
	if(ret){
		FREE_IOVEC(vec,count);
		jetnet_errno = EPARAMINVALID;
		return -1;
	}	
	if(vec && count == 1){
		return jetnet_tcp_send(ss, id, owner_leid, seq_id, vec[0].iov_base, vec[0].iov_len);
	}else{
		return jetnet_tcp_sendv_(ss, id, owner_leid, seq_id, vec, count);
	}
}

/*
return the socket id , -1 while occurse error.
*/
int jetnet_udp_create(jetnet_ss_t*ss, jetnet_leid_t owner_leid, int family){
	//alloc an empty socket object
	jetnet_socket_t *s = jetnet_alloc_socket(ss);
	if(!s){
		jetnet_errno = EALLOCSOCKET;
		return -1;
	}
	if(family != FAMILY_IPV6 && family != FAMILY_IPV4){
		jetnet_errno = ESOCKETFAMILY;
		return -1;
	}
	//new a socket handle and do listen work
    int s_fd = socket(family == FAMILY_IPV6 ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);
    if (s_fd < 0){
		jetnet_errno = errno;
		return -1;
	}
	//set up socket options
	if(jetnet_socket_noblock(s_fd,1) < 0){
		close(s_fd);
		jetnet_errno = errno;
		return -1;
	}
	if(jetnet_keep_alive(s_fd,1) < 0){
		close(s_fd);
		jetnet_errno = errno;
		return -1;
	}
	//associate the socket to the epoll
	if(jetnet_epoll_add(ss->event_fd, s_fd , EPOLLIN|EPOLLOUT|EPOLLRDHUP|EPOLLET , s) != 0){
		close(s_fd);
		jetnet_errno = errno;
		return -1;
	}
	//set up
	s->base.socket_p = SOCKET_P_UDP;
	s->base.socket_r = SOCKET_R_UNKNOW;
	s->base.parent_id = -1;
	s->base.owner_leid = owner_leid;
	s->base.establish_time = jetnet_time_now();
	s->fd = s_fd;
	s->can_send = true;
	s->can_recv = false;
	INIT_LIST_HEAD(&s->active_node);
	s->socket_state = SOCKET_STATE_CONNECTED;
	s->family = family;

	return s->base.id;
}

/*
return 0 when succeed , -1 while occurse error.
*/
int jetnet_udp_bind(jetnet_ss_t*ss, int id, const char * host, int port){
	jetnet_socket_t*s = jetnet_get_socket_org(ss,id);
	if(!s){
		jetnet_errno = ELOCATESOCKET;
		return -1;
	}

	if(host == NULL || host[0] == '\0')
		host = "0.0.0.0";
	
	int ret = 0;
	int family = FAMILY_UNKNOW;
	if(jetnet_is_ipv6(host)){
		family = FAMILY_IPV6;
	}else{
		family = FAMILY_IPV4;
	}
	if(family != s->family){
		jetnet_errno = ESOCKETFAMILY;
		return -1;
	}
	if( family == FAMILY_IPV6 ){
		struct sockaddr_in6 addr = { 0 };
		addr.sin6_family = AF_INET6;
		inet_pton(AF_INET6, host, &addr.sin6_addr);		
		addr.sin6_port  = htons(port);
		ret = bind(s->fd, (struct sockaddr*)(&addr), sizeof(struct sockaddr_in6));
	}else{
		struct sockaddr_in addr = { 0 };
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = inet_addr(host);
		addr.sin_port = htons(port);
		ret = bind(s->fd, (struct sockaddr*)(&addr), sizeof(struct sockaddr_in));
	}	
    if (ret < 0){
		jetnet_errno = errno;
		return -1;
    }
	return 0;
}

/*
return 0 when succeed , -1 while occurse error.
*/
int jetnet_udp_connect(jetnet_ss_t*ss, int id, const char * host, int port){
	jetnet_socket_t*s = jetnet_get_socket_org(ss,id);
	if(!s){
		jetnet_errno = ELOCATESOCKET;
		return -1;
	}
	if(host == NULL || host[0] == '\0'){
		jetnet_errno = EPARAMINVALID;
		return -1;
	}
	int ret = 0;
	int family = FAMILY_UNKNOW;
	if(jetnet_is_ipv6(host)){
		family = FAMILY_IPV6;
	}else{
		family = FAMILY_IPV4;
	}
	if( family == FAMILY_IPV6 ){
		struct sockaddr_in6 addr = { 0 };
		addr.sin6_family = AF_INET6;
		inet_pton(AF_INET6, host, &addr.sin6_addr);		
		addr.sin6_port  = htons(port);
		ret = connect(s->fd, (struct sockaddr*)(&addr), sizeof(struct sockaddr_in6));
	}else{
		struct sockaddr_in addr = { 0 };
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = inet_addr(host);
		addr.sin_port = htons(port);
		ret = connect(s->fd, (struct sockaddr*)(&addr), sizeof(struct sockaddr_in));
	}
    if (ret < 0){
		jetnet_errno = errno;
        return -1;
    }
	//set up
	int copyen_len  = MIN(ADDRESS_SIZE,strlen(host));
	memcpy(s->udp_address,host,copyen_len);
	s->udp_address[copyen_len] = 0;
	s->udp_port = port;
	return 0;
}

/*
return 0 if succeed , -1 while occurse error.
*/
int jetnet_udp_send(jetnet_ss_t*ss, int id, jetnet_leid_t owner_leid, uint32_t seq_id, void* data ,int len , const char * host, int port){
	if(data == NULL || len <= 0){
		jetnet_free(data);
		jetnet_errno = EPARAMINVALID;
		return -1;
	}	
	jetnet_socket_t*s = jetnet_get_socket_org(ss,id);
	if(!s){
		jetnet_free(data);
		jetnet_errno = ELOCATESOCKET;
		return -1;
	}
	// check whether the socket is udp socket
	if(s->base.socket_p != SOCKET_P_UDP){
		jetnet_free(data);
		jetnet_errno = EPROTOCOL;
		return -1;
	}
	// check state
	if(s->socket_state != SOCKET_STATE_CONNECTED){
		jetnet_free(data);
		jetnet_errno = ESOCKETSTATE;
		return -1;
	}
	//check host param
	if(s->udp_address[0] == '\0' && (host==NULL || host[0]=='\0')){
		jetnet_free(data);
		jetnet_errno = EPARAMINVALID;
		return -1;
	}	
	// if the socket is block to send now , just push the buffer to the write buffer list
	if(!s->can_send ){
		//append the data to wbl
		jetnet_swb_t*wb = (jetnet_swb_t *)jetnet_malloc(SIZEOF_UDPBUFFER);
		wb->seq_id = seq_id;
		wb->owner_leid = owner_leid;
		wb->base.vec[0].iov_base = data;
		wb->base.vec[0].iov_len = len;
		wb->base.vec_count = 1;		
		wb->base.offset = 0;
		memset(wb->udp_address,0,sizeof(wb->udp_address));
		wb->udp_port = 0;
		//cache the host information to write buffer if we hasn't called connect function
		if(s->udp_address[0] == 0){
			int copy_len  = MIN(ADDRESS_SIZE,strlen(host));
			memcpy(wb->udp_address,host,copy_len);
			wb->udp_address[copy_len] = 0;
			wb->udp_port = port;
		}
		jetnet_wbl_appen(&s->base.wbl, &wb->base);
		return 0;
	}
	else{
		if(jetnet_wbl_is_empty(&s->base.wbl)){
			//direct send
			int ret = 0;
			do{
				if( s->family == FAMILY_IPV6 ){
					struct sockaddr_in6 addr = { 0 };
					addr.sin6_family = AF_INET6;		
					if(s->udp_address[0] != 0){
						inet_pton(AF_INET6, s->udp_address, &addr.sin6_addr);		
						addr.sin6_port  = htons(s->udp_port);
					}
					else{
						inet_pton(AF_INET6, host, &addr.sin6_addr);		
						addr.sin6_port  = htons(port);
					}
					ret = sendto(s->fd, data , len , 0 , (struct sockaddr*)(&addr), sizeof(struct sockaddr_in6));
				}else{
					struct sockaddr_in addr = { 0 };
					addr.sin_family = AF_INET;		
					if(s->udp_address[0] != 0){
						addr.sin_addr.s_addr = inet_addr(s->udp_address);			
						addr.sin_port  = htons(s->udp_port);
					}
					else{
						addr.sin_addr.s_addr = inet_addr(host);
						addr.sin_port = htons(port);
					}
					ret = sendto(s->fd, data , len , 0 , (struct sockaddr*)(&addr), sizeof(struct sockaddr_in));
				}
			}while(ret<0 && errno==EINTR);
			
			if(ret < 0){
				if(errno == EAGAIN || errno == EWOULDBLOCK){
					jetnet_swb_t*wb = (jetnet_swb_t *)jetnet_malloc(SIZEOF_UDPBUFFER);
					wb->seq_id = seq_id;
					wb->owner_leid = owner_leid;
					wb->base.vec[0].iov_base = data;
					wb->base.vec[0].iov_len = len;
					wb->base.vec_count = 1;
					wb->base.offset = 0;
					memset(wb->udp_address,0,sizeof(wb->udp_address));
					wb->udp_port = 0;			
					//cache the host information
					if(s->udp_address[0] == 0){
						int copen_len  = MIN(ADDRESS_SIZE,strlen(host));
						memcpy(wb->udp_address,host,copen_len);
						wb->udp_address[copen_len] = 0;
						wb->udp_port = port;
					}
					jetnet_wbl_appen(&s->base.wbl, &wb->base);
					//set up the block flag
					s->can_send = false;
					return 0;
				}else{
					// free buffer
					jetnet_free(data);
					s->cache_errno = errno;
					s->socket_state = SOCKET_STATE_ERROR;
					//add to active links
					jetnet_active(ss,s);
					jetnet_errno = errno;
					return -1;
				}
			}else{
				jetnet_free(data);
				return 0;
			}
		}else{
			//push to wbl
			jetnet_swb_t*wb = (jetnet_swb_t *)jetnet_malloc(SIZEOF_UDPBUFFER);
			wb->seq_id = seq_id;
			wb->owner_leid = owner_leid;
			wb->base.vec[0].iov_base = data;
			wb->base.vec[0].iov_len = len;
			wb->base.vec_count = 1;
			wb->base.offset = 0;
			memset(wb->udp_address,0,sizeof(wb->udp_address));
			wb->udp_port = 0;			
			//cache the host information
			if(s->udp_address[0] == 0){
				int copen_len  = MIN(ADDRESS_SIZE,strlen(host));
				memcpy(wb->udp_address,host,copen_len);
				wb->udp_address[copen_len] = 0;
				wb->udp_port = port;
			}
			jetnet_wbl_appen(&s->base.wbl, &wb->base);
			return 0;
		}
	}
}

void jetnet_listener_handle_epoll_event(jetnet_ss_t*ss,jetnet_socket_t *s, jetnet_pe_t*ev){
	if(!s->can_recv){//remove from active links
		jetnet_deactive(s);
	}
	//do accept work.
	socklen_t len;
	union{
		struct sockaddr_in6 peer6;
		struct sockaddr_in peer4;
	}peer_addr;		
	if(s->family == FAMILY_IPV6){
		len = sizeof(struct sockaddr_in6);
	}else{
		len = sizeof(struct sockaddr_in);
	}
	int client_fd;
	do{
		if(s->family == FAMILY_IPV6){
			client_fd = accept(s->fd, (struct sockaddr*)&peer_addr.peer6, &len);
		}else{
			client_fd = accept(s->fd, (struct sockaddr*)&peer_addr.peer4, &len);
		}
	}while(client_fd<0 && errno==EINTR);
			
	if(client_fd < 0){
		switch(errno){
			case ECONNABORTED: //the socket has been closed by peer
			case EPROTO: //the socket has been closed by peer
			case EMFILE: //file max num limit
			case ENFILE: {
				//we don't do any thing ,and we keep socket in active links
				break;
			}
			case AGAIN_WOULDBLOCK:{
				// reset flag and remove from active links
				s->can_recv = false;
				jetnet_deactive(s);
				break;
			}
			default:{
				//accept socket error handler,we close this socket,and keep it in active links
				s->cache_errno = errno;
				s->socket_state = SOCKET_STATE_ERROR;
				ev->ev_type = POLL_EV_CLOSE;
				ev->data.close.errcode = errno;
				return;
			}
		}
	}else{
		//setup socket options
		if(jetnet_socket_noblock(client_fd,1) < 0){
			close(client_fd);
			return;
		}
		if(jetnet_socket_linger(client_fd,3) < 0){
			close(client_fd);
			return;
		}
		if(jetnet_socket_nagle(client_fd,0) < 0){
			close(client_fd);
			return;
		}
		if(jetnet_keep_alive(client_fd,1) < 0){
			close(client_fd);
			return;
		}
		//
		jetnet_socket_t *ns = jetnet_alloc_socket(ss);
		if(!ns){
			close(client_fd);
			return;
		}
		if(jetnet_epoll_add(ss->event_fd, client_fd, EPOLLIN|EPOLLOUT|EPOLLRDHUP|EPOLLET, ns) != 0){
			close(client_fd);
			return;
		}
		//set up
		ns->fd = client_fd;
		ns->base.socket_p = SOCKET_P_TCP;
		ns->base.socket_r = SOCKET_R_SERVER;
		ns->base.parent_id = s->base.id;
		ns->base.establish_time = jetnet_time_now();
		ns->family = s->family;
		ns->socket_state = SOCKET_STATE_CONNECTED;
		ns->can_send = true;
		ns->can_recv = false;
		INIT_LIST_HEAD(&ns->active_node);
		//gen an readalbe peer adress ,and report for app			
		int peer_port = 0;
		char peer_ip[ADDRESS_SIZE] = {0};			
		peer_port = ntohs(s->family == FAMILY_IPV4 ? peer_addr.peer4.sin_port : peer_addr.peer6.sin6_port);
		inet_ntop(s->family == FAMILY_IPV4 ? AF_INET : AF_INET6, s->family == FAMILY_IPV4 ? &peer_addr.peer4 : &peer_addr.peer4, peer_ip, sizeof(peer_ip));
		
		ev->ev_type = POLL_EV_ACCEPT;
		int copy_len = MIN(strlen(peer_ip),ADDRESS_SIZE-1);
		strncpy(ns->base.peer_host,peer_ip,copy_len);
		ns->base.peer_host[copy_len] = 0;
		ns->base.peer_port = peer_port;
		ev->s = &ns->base;
	}
}
 
void jetnet_connector_handle_epoll_event(jetnet_ss_t*ss,jetnet_socket_t *s, jetnet_pe_t*ev){
	//for connecting socket, we check the socket error state first
	int so_error = 0;
	socklen_t len = sizeof(so_error);  
	int ret = getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
	if(ret < 0){
		ev->ev_type = POLL_EV_CONNECT;
		ev->s = &s->base;
		ev->data.connect.result = 0;
		ev->data.connect.errcode = errno;
		s->cache_errno = errno;
		s->socket_state = SOCKET_STATE_ERROR;
		return;
	}	
	if(so_error > 0 && so_error != EAGAIN && so_error != EWOULDBLOCK){
		ev->ev_type = POLL_EV_CONNECT;
		ev->s = &s->base;
		ev->data.connect.result = 0;
		ev->data.connect.errcode = so_error;
		s->cache_errno = so_error;
		s->socket_state = SOCKET_STATE_ERROR;
		return;
	}
	//no error occurse ,if can_send hans't set , it means the socket hans't connect finish
	if(!s->can_send){
		jetnet_deactive(s);
		return;
	}
	
	//connect succeed.
	ev->ev_type = POLL_EV_CONNECT;
	ev->s = &s->base;
	ev->data.connect.result = 1;
	ev->data.connect.errcode = 0;
	//get peer address
	
	// int get_name_ret = 0;
	// int peer_port = 0;
	// char peer_ip[ADDRESS_SIZE] = {0};
	// socklen_t addrlen;
	// union{
		// struct sockaddr_in6 peer6;
		// struct sockaddr_in peer4;
	// }peer_addr;
	// if(s->family == FAMILY_IPV6){
		// addrlen = sizeof(struct sockaddr_in6);
		// get_name_ret = getpeername(s->fd, (struct sockaddr*)&peer_addr.peer6, &addrlen);
	// }else{
		// addrlen = sizeof(struct sockaddr_in);
		// get_name_ret = getpeername(s->fd, (struct sockaddr*)&peer_addr.peer4, &addrlen);
	// }		
	// if ( get_name_ret == 0) {
		// peer_port = ntohs(s->family == FAMILY_IPV4 ? peer_addr.peer4.sin_port : peer_addr.peer6.sin6_port);	
		// inet_ntop(s->family == FAMILY_IPV4 ? AF_INET : AF_INET6, s->family == FAMILY_IPV4 ? &peer_addr.peer4 : &peer_addr.peer4, peer_ip, sizeof(peer_ip));
	// }
	
	//now we change the state
	s->socket_state = SOCKET_STATE_CONNECTED;
	s->base.establish_time = jetnet_time_now();
	//and now we check wether we should remove from active link.
	if(!s->can_recv && ( !s->can_send || jetnet_wbl_is_empty(&s->base.wbl) == 1 ))
		jetnet_deactive(s);
}

void jetnet_closing_handle_epoll_event(jetnet_ss_t*ss,jetnet_socket_t *s, jetnet_pe_t*ev){
	//check whether is wbl is empty
	if(jetnet_wbl_is_empty(&s->base.wbl)){
		jetnet_force_close(ss,s);
		return;
	}
	
	if(s->can_send){
		int send_err;
		int send_cnt;
		int send_ret = jetnet_send_buffer_list(s,&send_err,&send_cnt);
		switch(send_ret){
			case 0:{ //no error,no data need to be send.
				jetnet_force_close(ss,s);
				return;
			}
			case 1:{ //no error,socket's buffer is not full
				if(jetnet_wbl_is_empty(&s->base.wbl)){
					jetnet_force_close(ss,s);
					return;
				}
				break;
			}
			case 2:{ //no error,socket's buffer is full
				s->can_send = false;
				break;
			}
			case -1:{ //get socket error
				jetnet_force_close(ss,s);
				return;
			}
		}
	}
	//it seems we don't need to care about the data input,but if the
	//flag is set,we try to detect error by read data.
	if(s->can_recv){
		int sz = s->recv_size;
		char * buffer = (char*)jetnet_malloc(sz);
		int read_ret = 0;
		do{
			read_ret = (int)read(s->fd, buffer, sz);			
		}while(read_ret < 0 && errno == EINTR);
		//we first to free the buffer,because we don't use it.
		jetnet_free(buffer);
		//check the result
		if (read_ret<0) {
			switch(errno) {
				case AGAIN_WOULDBLOCK:{
					//reset flag
					s->can_recv = false;
					break;
				}
				default:{
					jetnet_force_close(ss,s);
					return;
				}
			}
		}else if (read_ret==0) {//peer had closed
			jetnet_force_close(ss,s);
			return;
		}else{
			//update the static information
			s->base.total_recv_size += read_ret;
			s->base.total_recv_count++;
			//we got data
			if(read_ret < sz)
				s->can_recv = false;
			//calculate the new size
			s->recv_size = GET_RECV_SIZE(s->recv_size,read_ret);			
		}
	}
	//now we check wether we should remove from active links,to here,we know the wbl is not empty.
	if( !s->can_send && !s->can_recv)
		jetnet_deactive(s);
}

void jetnet_stream_handle_epoll_event(jetnet_ss_t*ss,jetnet_socket_t *s, jetnet_pe_t*ev){
	bool has_get_ev = false;
	//recv first
	if(s->can_recv){
		int sz = s->recv_size;
		char * buffer = (char*)jetnet_malloc(sz);
		int read_ret = 0;
		do{
			read_ret = (int)read(s->fd, buffer, sz);
		}while(read_ret < 0 && errno == EINTR);

		if (read_ret<0) {
			//free buffer first
			jetnet_free(buffer);
			switch(errno) {
				case AGAIN_WOULDBLOCK:{
					//reset flag
					s->can_recv = false;
					break;
				}
				default:{
					//clear
					s->socket_state = SOCKET_STATE_ERROR;
					s->cache_errno = errno;
					//we report close event and return
					ev->s = &s->base;
					ev->ev_type = POLL_EV_CLOSE;
					ev->data.close.errcode = errno;
					return;
				}
			}
		}else if (read_ret==0) {//peer had closed
			//no error occurse , but the peer has been close
			//free buffer
			jetnet_free(buffer);
			//clear
			s->socket_state = SOCKET_STATE_ERROR;
			s->cache_errno = 0;
			//we report close event and return
			ev->s = &s->base;
			ev->ev_type = POLL_EV_CLOSE;
			ev->data.close.errcode = 0;			
			return;
		}else{
			//update the static information
			s->base.total_recv_size += read_ret;
			s->base.total_recv_count++;
			//read data,push data to the read buffer list
			jetnet_rb_t*rb = (jetnet_rb_t*)jetnet_malloc(sizeof(jetnet_rb_t));
			rb->next = NULL;
			rb->buffer = buffer;
			rb->size = read_ret;
			jetnet_rbl_appen(&s->base.rbl,rb);
			if(read_ret < sz)
				s->can_recv = false;
			//calculate the new size
			s->recv_size = GET_RECV_SIZE(s->recv_size,read_ret);
			//we report the recv event
			ev->s = &s->base;
			ev->ev_type = POLL_EV_RECV;
			ev->data.recv.rb = rb;
			//we setup the flag
			has_get_ev = true;
		}
	}
	
	//check send
	if(s->can_send && jetnet_wbl_is_empty(&s->base.wbl) == 0){
		//try to send data
		int send_err;
		int send_cnt;
		int send_ret = jetnet_send_buffer_list(s,&send_err,&send_cnt);
		switch(send_ret){
			case 0:{ //no error,no data need to be send.
				break;
			}
			case 1:{ //no error,socket's buffer is not full
				break;
			}
			case 2:{ //no error,socket's buffer is full
				s->can_send = false;
				break;
			}
			case -1:{ //get socket error
				s->socket_state = SOCKET_STATE_ERROR;
				s->cache_errno = send_err;
				//check whether we should report close event
				if(!has_get_ev){
					ev->s = &s->base;
					ev->ev_type = POLL_EV_CLOSE;
					ev->data.close.errcode = send_err;
				}
				return;
			}
		}
	}	
	//active detect
	if(!s->can_recv && (!s->can_send || jetnet_wbl_is_empty(&s->base.wbl) == 1))
		jetnet_deactive(s);
}

void jetnet_udp_handle_epoll_event(jetnet_ss_t*ss, jetnet_socket_t *s, jetnet_pe_t*ev){

}

void jetnet_handle_socket_poll_event(jetnet_ss_t*ss, jetnet_socket_t *s, jetnet_pe_t*ev){
	if(s->socket_state == SOCKET_STATE_INVALID){
		assert(!"state error in poll");
		return;
	}
	if(s->socket_state == SOCKET_STATE_ERROR){
		ev->s = &s->base;
		ev->ev_type = POLL_EV_CLOSE;
		ev->data.close.errcode = s->cache_errno;
		return;
	}	
	if(s->base.socket_p == SOCKET_P_TCP){
		if(s->base.socket_r == SOCKET_R_LISTENER){
			jetnet_listener_handle_epoll_event(ss,s,ev);
		}else if(s->base.socket_r == SOCKET_R_CLIENT){
			if(s->socket_state == SOCKET_STATE_CONNECTING){				
				jetnet_connector_handle_epoll_event(ss,s,ev);
			}else{
				if(s->socket_state == SOCKET_STATE_CLOSING ){
					jetnet_closing_handle_epoll_event(ss,s,ev);
				}else{
					jetnet_stream_handle_epoll_event(ss,s,ev);
				}
			}
		}else if(s->base.socket_r == SOCKET_R_SERVER){
				if(s->socket_state == SOCKET_STATE_CLOSING ){
					jetnet_closing_handle_epoll_event(ss,s,ev);
				}else{
					jetnet_stream_handle_epoll_event(ss,s,ev);
				}
		}else{
			assert(!"internal error!");
		}
	}else if(s->base.socket_p == SOCKET_P_UDP){
		jetnet_udp_handle_epoll_event(ss,s,ev);
	}else{
		assert(!"internal error!");
	}
}

static int jetnet_ss_check_round(jetnet_ss_t*ss){
	if(list_empty(&ss->active_list))
		return 1;
	jetnet_socket_t* s = list_entry(ss->active_list.next,jetnet_socket_t, active_node);
	if( s->active_token == ss->active_token )
		return 1;
	return 0;
}

int jetnet_ss_poll(jetnet_ss_t*ss, jetnet_pe_t*ev, int timeout){
	//we close the socket in a delete state
	jetnet_socket_t*pos , *n;
	list_for_each_entry_safe(pos, n, &ss->close_list, close_node){
		jetnet_force_close(ss,pos);
	}	
	//set up return
	ev->ev_type = POLL_EV_NONE;
	ev->s = NULL;
	
	//if the active links is empty,we try to get task from epoll
	if(jetnet_ss_check_round(ss)){
		struct epoll_event epoll_ev[MAX_EVENT];
		int wait_ret = 0;
		do{
			wait_ret = jetnet_epoll_wait(ss->event_fd, epoll_ev, MAX_EVENT,timeout);
		}while(wait_ret < 0 && errno == EINTR);
		
		if(wait_ret < 0){
			jetnet_errno = errno;
			return -1;
		}
		int i;
		for(i = 0 ; i < wait_ret ; i++){
			jetnet_socket_t *s = (jetnet_socket_t *)epoll_ev[i].data.ptr;
			if(jetnet_epoll_has_in(epoll_ev[i].events))
				s->can_recv = true;
			if(jetnet_epoll_has_out(epoll_ev[i].events))
				s->can_send = true;
			if(jetnet_epoll_has_error(epoll_ev[i].events)){
				if(s->base.socket_r == SOCKET_R_LISTENER){//listen socket hans't out flag
					s->can_recv = true;
				}else{
					s->can_recv = true;
					s->can_send = true;
				}
			}
			//we check whether the flag influence the active state
			if(!jetnet_is_active(s)){
				if(s->socket_state == SOCKET_STATE_CONNECTING){
					jetnet_active(ss,s);
				}else if(s->can_recv || (s->can_send && jetnet_wbl_is_empty(&s->base.wbl) == 0 )){
					jetnet_active(ss,s);
				}
			}
		}
		if(wait_ret > 0)
			ss->active_token ++;
	}
	//no event get
	struct list_head * active_head = list_pop_head(&ss->active_list);
	if(!active_head)
		return 0;
	list_add_tail(active_head,&ss->active_list);
	jetnet_socket_t*current_s = list_entry(active_head,jetnet_socket_t,active_node);
	jetnet_handle_socket_poll_event(ss,current_s,ev);
	//if we get an socket close event, we should put the socket in to the delete list
	if(ev->ev_type == POLL_EV_CLOSE && !jetnet_has_close_flag((jetnet_socket_t*)ev->s))
		jetnet_set_close_flag(ss,(jetnet_socket_t*)ev->s);
	return 1;
}