#ifndef JETNET_NS_H_INCLUDED
#define JETNET_NS_H_INCLUDED

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include "jetnet_ss.h"
#include "jetnet_proto.h"
#include "jetnet_cmd.h"
#include "jetnet_mq.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * init net service object.
 */
int jetnet_ns_init();

/**
 * release net service object.
 */
void jetnet_ns_release();

/**
 * return the socket id , -1 while occurse error.
 */
int jetnet_net_listen(jetnet_leid_t owner_leid, int protocol, const char * host, int port, int backlog);

/**
 * return the socket id , -1 while occurse error.
 */
int jetnet_net_connect(jetnet_leid_t owner_leid, int protocol, const char * host, int port);

/**
 * return 0 if succeed , -1 while occurse error.
 */
int jetnet_net_send(int id, jetnet_leid_t owner_leid, uint32_t seq_id, void* data, int len);

/**
 * return 0 if succeed , -1 while occurse error.
 */
int jetnet_net_close(int id);

/**
*return 1 if the socket is connected otherwise return 0
*we should call this function after we call jetnet_tcp_connect to check
*the socket whether connected succeed immediate.
**/
int jetnet_net_is_connected(int id);

/**
 * poll notify from net service.return 0 if succeed , -1 while occurse error.
 */
int jetnet_net_poll(jetnet_mq_t*mq, int timeout);

/**
 * get socket service object from net service object.
 */
jetnet_ss_t* jetnet_get_ss();

#if defined(__cplusplus)
}
#endif

#endif /* JETNET_NS_H_INCLUDED */
