#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>

#include "jetnet_ultil.h"

int jetnet_str2i(const char* s)
{  
    int i;  
    int n = 0;  
    for (i = 0; s[i] >= '0' && s[i] <= '9'; ++i)
        n = 10 * n + (s[i] - '0');
    return n;  
}  

int jetnet_hexstr2i(const char* s)
{  
    int i;  
    int n = 0;  
    if (s[0] == '0' && (s[1]=='x' || s[1]=='X'))
        i = 2;
	else
        i = 0;
    for (; (s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'z') || (s[i] >='A' && s[i] <= 'Z');++i){  
        if (tolower(s[i]) > '9')
            n = 16 * n + (10 + tolower(s[i]) - 'a');
        else
            n = 16 * n + (tolower(s[i]) - '0');
    }
    return n;
}

int jetnet_s2i(const char *s)  
{
	if (s[0] == '0' && (s[1]=='x' || s[1]=='X'))
		return jetnet_hexstr2i(s);
	return jetnet_str2i(s);
}

uint32_t jetnet_ip_s2i(const char *s){
	struct in_addr inp;
	if( inet_aton(s, &inp) == 0 )
		return (uint32_t)-1;
	return (uint32_t)inp.s_addr;
}

const char* jetnet_ip_i2s(uint32_t ip){
	struct in_addr addr;
	addr.s_addr = ip;
	return inet_ntoa(addr);
}
