#ifndef JETNET_PROTO_H_INCLUDED
#define JETNET_PROTO_H_INCLUDED

#include <stdint.h>

#include "jetnet_mq.h"
#include "jetnet_ss.h"
#include "jetnet_api.h"

#if defined(__cplusplus)
extern "C" {
#endif

//parser
typedef void* (*jetnet_pf_malloc_pi)(int socket_r);
typedef void (*jetnet_pf_free_pi)(void*pi);
typedef void (*jetnet_pf_parser_reset)(void*pi);
typedef int (*jetnet_pf_parser)(jetnet_rbl_t *rbl, jetnet_rb_t *rb, void*pi);

//define the protocol poll function type
typedef void (*jetnet_pf_poll)(jetnet_ss_t*ss, jetnet_mq_t*mq);
//define the socket event handle function type.
typedef void (*jetnet_pf_ev_handler)(jetnet_ss_t*ss, jetnet_pe_t*ev, jetnet_mq_t*mq);

//define API to wrap the user protocol operation
typedef int (*jetnet_pf_listen)(jetnet_ss_t*ss, jetnet_leid_t owner_leid, const char * host, int port, int backlog);
typedef int (*jetnet_pf_connect)(jetnet_ss_t*ss, jetnet_leid_t owner_leid, const char * host, int port);
typedef int (*jetnet_pf_send)(jetnet_ss_t*ss, int id, jetnet_leid_t owner_leid, uint32_t seq_id, void* data, int len);
typedef void (*jetnet_pf_close)(jetnet_ss_t*ss, int id);

typedef struct jetnet_proto_t{
	jetnet_pf_malloc_pi f_malloc_pi;
	jetnet_pf_free_pi free_pi;
	jetnet_pf_parser_reset parser_reset;
	jetnet_pf_parser parser;
	jetnet_pf_poll poll;
	jetnet_pf_ev_handler ev_handler;
	jetnet_pf_listen listen;
	jetnet_pf_connect connect;
	jetnet_pf_send send;
	jetnet_pf_close close;
}jetnet_proto_t;

//protocol data for listener socket
typedef struct jetnet_base_pd_t{
	int protocol;
}jetnet_base_pd;


#if defined(__cplusplus)
}
#endif

#endif /* JETNET_PROTO_H_INCLUDED */