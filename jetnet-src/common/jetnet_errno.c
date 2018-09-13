#include <stdio.h>
#include <string.h>
#include "jetnet_errno.h"


#define	MIN_JETNET_ERRNO	10000

int jetnet_errno = 0;

char* jetnet_err_str_array[] = {
	"can't alloc socket",
	"param is invalid",
	"operation fail in the current socket state",
	"can't locate the socket by provide id",
	"the protocol is unsported or protocol data parser error",
	"the operation is unsported on this socket",
	"the socket family is unspport",
	"out of memory",
	"valid fail",
	"parser protocol data fail",
	"serialize protocol fail",
};

int jetnet_err_str_size = sizeof(jetnet_err_str_array)/sizeof(jetnet_err_str_array[0]);

char* jetnet_strerror(int err){
	if(err < MIN_JETNET_ERRNO){
		return strerror(err);
	}
	if(err < MIN_JETNET_ERRNO + jetnet_err_str_size)
		return jetnet_err_str_array[err-MIN_JETNET_ERRNO];
	return "invalid error code!";
}
