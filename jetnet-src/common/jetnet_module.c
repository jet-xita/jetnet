#include <assert.h>
#include <string.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "jetnet_module.h"
#include "jetnet_const.h"
#include "jetnet_malloc.h"

typedef struct jetnet_module_mgr_t{
	char path[256];
	int size;
	jetnet_module_t modules[MAX_MODULE_TYPE];
}jetnet_module_mgr_t;

jetnet_module_mgr_t* g_module_mgr = NULL;

static void * _try_open(jetnet_module_mgr_t *mgr, const char * name) {
	const char *l;
	const char * path = mgr->path;
	size_t path_size = strlen(path);
	size_t name_size = strlen(name);

	int sz = path_size + name_size;
	//search path
	void * dl = NULL;
	char tmp[sz];
	do
	{
		memset(tmp,0,sz);
		while (*path == ';') path++;
		if (*path == '\0') break;
		l = strchr(path, ';');
		if (l == NULL) l = path + strlen(path);
		int len = l - path;
		int i;
		for (i=0;path[i]!='?' && i < len ;i++) {
			tmp[i] = path[i];
		}
		memcpy(tmp+i,name,name_size);
		if (path[i] == '?') {
			strncpy(tmp+i+name_size,path+i+1,len - i - 1);
		} else {
			fprintf(stderr,"Invalid C module path\n");
			exit(1);
		}
		dl = dlopen(tmp, RTLD_NOW | RTLD_GLOBAL);
		path = l;
	}while(dl == NULL);

	if (dl == NULL) {
		fprintf(stderr, "try open %s failed : %s\n",name,dlerror());
	}

	return dl;
}

static jetnet_module_t* _query(jetnet_module_mgr_t* mgr, const char * name) {
	int i;
	for (i=0;i<mgr->size;i++) {
		if (strcmp(mgr->modules[i].name,name)==0) {
			return &mgr->modules[i];
		}
	}
	return NULL;
}

static void * get_api(jetnet_module_t *m, const char *api_name) {
	size_t name_size = strlen(m->name);
	size_t api_size = strlen(api_name);
	char tmp[name_size + api_size + 1];
	memcpy(tmp, m->name, name_size);
	memcpy(tmp+name_size, api_name, api_size+1);
	char *ptr = strrchr(tmp, '.');
	if (ptr == NULL) {
		ptr = tmp;
	} else {
		ptr = ptr + 1;
	}
	return dlsym(m->module, ptr);
}

static int open_sym(jetnet_module_t *m) {
	m->dl_init = get_api(m, "_init");
	m->dl_startup = get_api(m, "_startup");
	return m->dl_init == NULL?0:1;
}

int jetnet_module_init(const char *path){
	if(g_module_mgr)
		return 0;
	g_module_mgr = (jetnet_module_mgr_t*)jetnet_malloc(sizeof(jetnet_module_mgr_t));
	g_module_mgr->size = 0;
	strcpy(g_module_mgr->path,path);
	memset(g_module_mgr->modules,0,sizeof(g_module_mgr->modules));
	return 1;
}

void jetnet_module_release(){
	if(!g_module_mgr)
		return;
	int i;
	for(i = 0; i < g_module_mgr->size; i++){
		dlclose(g_module_mgr->modules[i].module);
	}
	jetnet_free(g_module_mgr);
	g_module_mgr = NULL;
}

jetnet_module_t * jetnet_module_query(const char * name) {
	if(!g_module_mgr)
		return NULL;
	jetnet_module_t* result = _query(g_module_mgr, name);
	if (result)
		return result;
	if (result == NULL && g_module_mgr->size < MAX_MODULE_TYPE) {
		int index = g_module_mgr->size;
		void * dl = _try_open(g_module_mgr,name);
		if (dl) {
			strcpy(g_module_mgr->modules[index].name,name);
			g_module_mgr->modules[index].module = dl;
			if (open_sym(&g_module_mgr->modules[index])){
				g_module_mgr->size ++;
				result = &g_module_mgr->modules[index];
			}
		}
	}
	return result;
}

int jetnet_module_get_size(){
	if(!g_module_mgr)
		return 0;
	return g_module_mgr->size;
}

jetnet_module_t * jetnet_module_get(int index){
	if(!g_module_mgr)
		return NULL;
	if( index < 0 || index >= g_module_mgr->size )
		return NULL;
	return &g_module_mgr->modules[index];
}