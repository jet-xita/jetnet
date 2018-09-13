#include <string.h>
#include "jetnet_cell_cfg.h"
#include "jetnet_malloc.h"
#include "jetnet_ultil.h"
#include "ezxml.h"

int jetnet_insert_cell_cfg(struct rb_root *root, jetnet_cell_cfg_item_t *data)
{
  	struct rb_node **new = &(root->rb_node), *parent = NULL;
	int result = 0;
  	/* Figure out where to put new node */
  	while (*new) {
  		jetnet_cell_cfg_item_t *this = rb_entry(*new, jetnet_cell_cfg_item_t, node_by_name);
		parent = *new;
		result = strcmp(data->cell_name,this->cell_name);
  		if (result < 0)
  			new = &((*new)->rb_left);
  		else if (result > 0)
  			new = &((*new)->rb_right);
  		else
  			return 0;
  	}
  	/* Add new node and rebalance tree. */
  	rb_link_node(&data->node_by_name, parent, new);
  	rb_insert_color(&data->node_by_name, root);
	return 1;
}

jetnet_cell_cfg_t*		jetnet_load_cell_cfg(const char*config_file){
	if(strlen(config_file) == 0)
		return NULL;
	ezxml_t root = ezxml_parse_file(config_file);
	if(!root)
		return NULL;
	jetnet_cell_cfg_t* ret = (jetnet_cell_cfg_t*)jetnet_malloc(sizeof(jetnet_cell_cfg_t));
	ret->name2cell = RB_ROOT;
	ezxml_t cell_node;
	for (cell_node = ezxml_child(root, "cell"); cell_node; cell_node = cell_node->next) {
		jetnet_cell_cfg_item_t*item = (jetnet_cell_cfg_item_t*)jetnet_malloc(sizeof(jetnet_cell_cfg_item_t));
		memset(item,0,sizeof(jetnet_cell_cfg_item_t));
		//name
		const char * name = ezxml_attr(cell_node, "name");
		if(!name){
			jetnet_free(item);
			jetnet_free_cell_cfg(ret);
			ezxml_free(root);
			return NULL;
		}
		strcpy(item->cell_name,name);
		//cip
		ezxml_t ch_node = ezxml_child(cell_node, "cip");
		if(!ch_node){
			jetnet_free(item);
			jetnet_free_cell_cfg(ret);
			ezxml_free(root);
			return NULL;
		}
		item->cip = jetnet_ip_s2i(ch_node->txt);

		//cport
		ch_node = ezxml_child(cell_node, "cport");
		if(!ch_node){
			jetnet_free(item);
			jetnet_free_cell_cfg(ret);
			ezxml_free(root);
			return NULL;
		}
		item->cport = (uint16_t)jetnet_s2i(ch_node->txt);

		//cno
		ch_node = ezxml_child(cell_node, "cno");
		if(!ch_node){
			jetnet_free(item);
			jetnet_free_cell_cfg(ret);
			ezxml_free(root);
			return NULL;
		}
		item->cno = (uint16_t)jetnet_s2i(ch_node->txt);	
		//channel_size
		ch_node = ezxml_child(cell_node, "channel_size");
		if(!ch_node){
			jetnet_free(item);
			jetnet_free_cell_cfg(ret);
			ezxml_free(root);
			return NULL;
		}
		item->channel_size = (unsigned int)jetnet_s2i(ch_node->txt);
		//hub_cno
		ch_node = ezxml_child(cell_node, "hub_cno");
		if(!ch_node){
			jetnet_free(item);
			jetnet_free_cell_cfg(ret);
			ezxml_free(root);
			return NULL;
		}
		item->hub_cno = (uint16_t)jetnet_s2i(ch_node->txt);	
		//hub_eno
		ch_node = ezxml_child(cell_node, "hub_eno");
		if(!ch_node){
			jetnet_free(item);
			jetnet_free_cell_cfg(ret);
			ezxml_free(root);
			return NULL;
		}
		item->hub_eno = (uint32_t)jetnet_s2i(ch_node->txt);	
		//module_path
		ch_node = ezxml_child(cell_node, "module_path");
		if(!ch_node){
			jetnet_free(item);
			jetnet_free_cell_cfg(ret);
			ezxml_free(root);
			return NULL;
		}
		strcpy(item->module_path, ch_node->txt);

		//modules
		ch_node = ezxml_child(cell_node, "modules");
		if(!ch_node){
			jetnet_free(item);
			jetnet_free_cell_cfg(ret);
			ezxml_free(root);
			return NULL;
		}		
		int module_count = 0;
		char *mstr = NULL;
		mstr = strtok(ch_node->txt, ",");
		while( mstr != NULL )
		{
			item->modules[module_count] = (char*)jetnet_malloc(strlen(mstr)+1);
			strcpy(item->modules[module_count],mstr);
			module_count++;
			mstr = strtok(NULL, ",");
		}
		jetnet_insert_cell_cfg(&ret->name2cell,item);
	}
	ezxml_free(root);
	return ret;
}

static void jetnet_free_cfg_item(jetnet_cell_cfg_item_t *item){
	int i;
	for(i=0; i < MAX_MODULE_TYPE; i++){
		if(!item->modules[i])
			break;
		jetnet_free(item->modules[i]);
	}
	jetnet_free(item);
}

static void jetnet_free_cfg_item_node(struct rb_node *node){
	if(!node)
		return;
	if(node->rb_left)
		jetnet_free_cfg_item_node(node->rb_left);
	if(node->rb_right)
		jetnet_free_cfg_item_node(node->rb_right);
	jetnet_cell_cfg_item_t *item = rb_entry(node, jetnet_cell_cfg_item_t, node_by_name);
	jetnet_free_cfg_item(item);	
}

void	jetnet_free_cell_cfg(jetnet_cell_cfg_t*cfg){
	jetnet_free_cfg_item_node(cfg->name2cell.rb_node);
	jetnet_free(cfg);
}

jetnet_cell_cfg_item_t* jetnet_find_cell_cfg(jetnet_cell_cfg_t*info, const char* cell_name){
	struct rb_node *node = info->name2cell.rb_node;
	int result;
  	while (node) {
  		jetnet_cell_cfg_item_t *item = rb_entry(node, jetnet_cell_cfg_item_t, node_by_name);		
		result = strcmp(cell_name,item->cell_name);
		if (result < 0)
  			node = node->rb_left;
		else if (result > 0)
  			node = node->rb_right;
		else
  			return item;
	}
	return NULL;
}
