#ifndef JETNET_TCPPKG_H_INCLUDED
#define JETNET_TCPPKG_H_INCLUDED

#include "jetnet_proto.h"

#if defined(__cplusplus)
extern "C" {
#endif


jetnet_proto_t* jetnet_tcppkg_create();
void jetnet_tcppkg_release();

#if defined(__cplusplus)
}
#endif

#endif /* JETNET_TCPPKG_H_INCLUDED */