#ifndef JETNET_SERVER_H_INCLUDED
#define JETNET_SERVER_H_INCLUDED
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif

int jetnet_server_init(const char*cfg_file, const char*server_name);
void jetnet_server_release();
void jetnet_server_loop();


#if defined(__cplusplus)
}
#endif

#endif /* JETNET_SERVER_H_INCLUDED */