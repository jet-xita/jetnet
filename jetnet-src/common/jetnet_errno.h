#ifndef JETNET_ERRNO_H_INCLUDED
#define JETNET_ERRNO_H_INCLUDED

#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * declare the error no variable.
 */
extern int jetnet_errno;

/**
 * conver error no to error description string.
 */
char*   jetnet_strerror(int err);

/**
 * application defined error code must start with 10000,and increase 1 every code.
 */
#define EALLOCSOCKET  		10000   //can't alloc socket
#define EPARAMINVALID		10001	//param is invalid
#define	ESOCKETSTATE		10002	//operation fail in the current socket state
#define	ELOCATESOCKET		10003	//can't locate the socket by provide id
#define	EPROTOCOL			10004	//the protocol is unsported or protocol data parser error
#define	ESOCKETROLE			10005	//the operation is unsported on this socket
#define ESOCKETFAMILY		10006	//the socket family is unspport
#define EMEMORY				10007	//out of memory
#define EVALID				10008	//valid fail
#define EPARSER				10009	//parser protocol data fail
#define ESERIALIZE			10010	//serialize protocol fail

#if defined(__cplusplus)
}
#endif

#endif /* JETNET_ERRNO_H_INCLUDED */