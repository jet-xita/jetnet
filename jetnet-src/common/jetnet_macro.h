#ifndef __JETNET_MACRO_H_INCLUDED
#define __JETNET_MACRO_H_INCLUDED

#if defined(__cplusplus)
extern "C" {
#endif

#ifndef MIN
# define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
# define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#if defined(__cplusplus)
}
#endif

#endif /* __JETNET_MACRO_H_INCLUDED */