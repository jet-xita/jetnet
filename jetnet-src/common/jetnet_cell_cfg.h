#ifndef __JETNET_CELL_CFG_H_INCLUDED
#define __JETNET_CELL_CFG_H_INCLUDED
#include <stdint.h>
#include "rbtree.h"
#include "jetnet_const.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct jetnet_cell_cfg_item_t{
	char cell_name[64];
	uint32_t cip;
	uint16_t cport;
	uint16_t cno;
	unsigned int channel_size;
	uint16_t hub_cno;
	uint32_t hub_eno;
	char module_path[256];	
	char* modules[MAX_MODULE_TYPE];	
	struct rb_node node_by_name;
}jetnet_cell_cfg_item_t;

typedef struct jetnet_cell_cfg_t{
	struct rb_root	name2cell;
}jetnet_cell_cfg_t;

/**
 * create a server config object.
 */
jetnet_cell_cfg_t* jetnet_load_cell_cfg(const char*config_file);

/**
 * free a server config object.
 */
void	jetnet_free_cell_cfg(jetnet_cell_cfg_t*cfg);

/**
 * find cell config item object by server name.
 */
jetnet_cell_cfg_item_t* jetnet_find_cell_cfg(jetnet_cell_cfg_t*cfg, const char* cell_name);

#if defined(__cplusplus)
}
#endif

#endif /* __JETNET_CELL_CFG_H_INCLUDED */