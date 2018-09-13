#ifndef JETNET_TCPSAC_H_INCLUDED
#define JETNET_TCPSAC_H_INCLUDED

#include "jetnet_proto.h"

#if defined(__cplusplus)
extern "C" {
#endif

jetnet_proto_t* jetnet_tcpsac_create();
void jetnet_tcpsac_release();

#if defined(__cplusplus)
}
#endif

#endif /* JETNET_TCPSAC_H_INCLUDED */