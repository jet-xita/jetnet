#ifndef JETNET_MODULE_H_INCLUDE
#define JETNET_MODULE_H_INCLUDE
#include "jetnet_api.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef int (*jetnet_dl_init)(jetnet_api_t*api);
typedef void (*jetnet_dl_startup)(jetnet_api_t*api);

typedef struct jetnet_module_t{
	char name[64];
	void *module;
	jetnet_dl_init dl_init;
	jetnet_dl_startup dl_startup;
}jetnet_module_t;

int jetnet_module_init(const char *path);
void jetnet_module_release();
jetnet_module_t * jetnet_module_query(const char * name);
int jetnet_module_get_size();
jetnet_module_t * jetnet_module_get(int index);

#if defined(__cplusplus)
}
#endif

#endif /*JETNET_MODULE_H_INCLUDE*/
