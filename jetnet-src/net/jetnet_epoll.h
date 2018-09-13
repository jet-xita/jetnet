#ifndef JETNET_EPOLL_H_INCLUDED
#define JETNET_EPOLL_H_INCLUDED

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>


#if defined(__cplusplus)
extern "C" {
#endif

/**
 * open an epoll file descriptor.returns a non-negative 
 * integer identifying the descriptor or -2 when an error occurs.
 */
static inline int jetnet_epoll_create(){
	return epoll_create(1000000);
}

/**
 * close epoll file descriptor which is return by jetnet_epoll_create().
 */
static inline void jetnet_epoll_release(int efd){
	close(efd);
}

/**
 * add socket handle to epoll.
 */
static inline int jetnet_epoll_add(int efd, int sock, uint32_t flag , void *ud){
	//flag = flag | EPOLLOUT | EPOLLIN | EPOLLRDHUP;	
	struct epoll_event ev;
	ev.events = flag;
	ev.data.ptr = ud;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, sock, &ev) == -1) {
		return 1;
	}
	return 0;
}

/**
 * remove socket handle from epoll.
 */
static inline int jetnet_epoll_mod(int efd, int sock, uint32_t flag , void *ud){
	//return 0;	
	struct epoll_event ev;
	ev.events = flag;
	ev.data.ptr = ud;
	if (epoll_ctl(efd, EPOLL_CTL_MOD, sock, &ev) == -1) {
		return 1;
	}
	return 0;
}

/**
 * get the current time in microsecond.
 */
static inline void jetnet_epoll_del(int efd, int sock){
	epoll_ctl(efd, EPOLL_CTL_DEL, sock , NULL);
}

/**
 * wait epoll events.
 */
static inline int jetnet_epoll_wait(int efd, struct epoll_event *ev, int max , int timeout){
	return epoll_wait(efd , ev, max, timeout);
}

/**
 * check whether there has epoll_out event flag in the events.
 */
static inline bool jetnet_epoll_has_out(int events){
	return events&EPOLLOUT?true:false;
}

/**
 * check whether there has epoll_in event flag in the events.
 */
static inline bool jetnet_epoll_has_in(int events){
	return events&EPOLLIN?true:false;
}

/**
 * check whether there has epoll_err event flag in the events.
 */
static inline bool jetnet_epoll_has_error(int events){
	return events&(EPOLLERR|EPOLLHUP|EPOLLRDHUP)?true:false;
}

#if defined(__cplusplus)
}
#endif

#endif /* JETNET_EPOLL_H_INCLUDED */
