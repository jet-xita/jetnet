#ifndef JETNET_MALLOC_H_INCLUDED
#define JETNET_MALLOC_H_INCLUDED
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#if defined(__cplusplus)
extern "C" {
#endif

#ifndef __USE_MEMPOOL__
	#define jetnet_malloc malloc
	#define jetnet_calloc calloc
	#define jetnet_realloc realloc
	#define jetnet_free free
	#define jetnet_memalign memalign
#else
	void * jetnet_malloc(size_t sz);
	void * jetnet_calloc(size_t nmemb,size_t size);
	void * jetnet_realloc(void *ptr, size_t size);
	void   jetnet_free(void *ptr);
	void * jetnet_memalign(size_t alignment, size_t size);
#endif

#define JETNET_MALLOC(t) ((t)*)jetnet_malloc(sizeof(t))

#if defined(__cplusplus)
}
#endif

#endif /* JETNET_MALLOC_H_INCLUDED */
