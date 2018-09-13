#ifndef __JETNET_ULTIL_H_INCLUDED
#define __JETNET_ULTIL_H_INCLUDED
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * convert a 10 decimal string to integer
 */
int jetnet_str2i(const char* s);

/**
 * convert a hex string to integer
 */
int jetnet_hexstr2i(const char* s);

/**
 * convert string to integer
 */
int jetnet_s2i(const char* s);

/**
 * convert string to integer
 */
uint32_t jetnet_ip_s2i(const char *s);

/**
 * convert integer ip to string
 */
const char* jetnet_ip_i2s(uint32_t ip);

#if defined(__cplusplus)
}
#endif

#endif /* __JETNET_ULTIL_H_INCLUDED */