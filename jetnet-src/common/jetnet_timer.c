#include "jetnet_timer.h"
#include "jetnet_malloc.h"
#include "jetnet_time.h"
#include "rbtree.h"
#include <stdlib.h>
#include <string.h>

#define JETNET_TIMER_HEAP_SIZE 256
#define JETNET_TIMER_TRIGGER_HEAP_SIZE 64

#define JETNET_HEAP_LEFT_CHILD(index) (((index) << 1) + 1)
#define JETNET_HEAP_RIGHT_CHILD(index) (((index) << 1) + 2)
#define JETNET_HEAP_PARENT(index) (((index)-1)>>1)
#define JETNET_HEAP_LAST_NO_LEAF(size) (((size)-1)>>1)

#define JETNET_HEAP_SET_ITEM(mgr,item,index) ((mgr)->timer_heap[index] = (item))		
#define JETNET_TRIGGER_HEAP_SET_ITEM(mgr,item,index) ((mgr)->trigger_heap[index] = (item))

#define JETNET_HEAP_SWAP(mgr,index1,index2)\
	do{\
		jetnet_timer_item_t*temp = (mgr)->timer_heap[index1];\
		JETNET_HEAP_SET_ITEM(mgr,(mgr)->timer_heap[index2],index1);\
		JETNET_HEAP_SET_ITEM(mgr,temp,index2);\
	}while(0)
		
#define JETNET_HEAP_LESS_THAN(mgr,index1,index2)\
	((mgr)->timer_heap[(index1)]->expire < (mgr)->timer_heap[(index2)]->expire)

typedef struct jetnet_timer_item_t{
	int timer_id;
	jetnet_timer_cb cb;
	void*data;
	void*ud;
	int delay;
	int period;
	int repeat;	
	unsigned long expire;
}jetnet_timer_item_t;

typedef struct jetnet_timer_id_node_t{
	jetnet_timer_item_t*it;
	struct rb_node node;
}jetnet_timer_id_node_t;

typedef struct jetnet_timer_mgr_t{
	jetnet_timer_item_t** timer_heap;
	int	heap_size;
	int heap_cap;
	jetnet_timer_item_t** trigger_heap;
	int	trigger_size;
	int trigger_cap;
	struct rb_root	troot;
	unsigned int timer_id_counter;
}jetnet_timer_mgr_t;

jetnet_timer_mgr_t* g_timer_mgr = NULL;

static void jetnet_timer_free_tree_node(struct rb_node *node){
	if(!node)
		return;
	if(node->rb_left)
		jetnet_timer_free_tree_node(node->rb_left);
	if(node->rb_right)
		jetnet_timer_free_tree_node(node->rb_right);
	jetnet_timer_id_node_t *item = container_of(node, jetnet_timer_id_node_t, node);
	jetnet_free(item);	
}

static int jetnet_timer_add_tree_node(struct rb_root *root, jetnet_timer_id_node_t *data)
{
  	struct rb_node **new = &(root->rb_node), *parent = NULL;
  	/* Figure out where to put new node */
  	while (*new) {
  		jetnet_timer_id_node_t *this = container_of(*new, jetnet_timer_id_node_t, node);
		parent = *new;
  		if (data->it->timer_id < this->it->timer_id)
  			new = &((*new)->rb_left);
  		else if (data->it->timer_id > this->it->timer_id)
  			new = &((*new)->rb_right);
  		else
  			return 0;
  	}
  	/* Add new node and rebalance tree. */
  	rb_link_node(&data->node, parent, new);
  	rb_insert_color(&data->node, root);
	return 1;
}

static jetnet_timer_id_node_t* jetnet_timer_find_tree_node(struct rb_root *root, unsigned int timer_id)
{
  	struct rb_node **new = &(root->rb_node), *parent = NULL;
  	/* Figure out where to put new node */
  	while (*new) {
  		jetnet_timer_id_node_t *this = container_of(*new, jetnet_timer_id_node_t, node);
		parent = *new;
  		if (timer_id < this->it->timer_id)
  			new = &((*new)->rb_left);
  		else if (timer_id > this->it->timer_id)
  			new = &((*new)->rb_right);
  		else
  			return this;
  	}
	return NULL;
}

static void jetnet_timer_heap_expand(jetnet_timer_mgr_t*mgr){
	int new_cap = mgr->heap_cap<<1;
	jetnet_timer_item_t** new_heap = (jetnet_timer_item_t**)jetnet_malloc(sizeof(jetnet_timer_item_t*)*new_cap);
	memcpy(new_heap,mgr->timer_heap,sizeof(jetnet_timer_item_t*)*mgr->heap_cap);
	jetnet_free(mgr->timer_heap);
	mgr->heap_cap = new_cap;
	mgr->timer_heap = new_heap;
}

static void jetnet_timer_trigger_heap_expand(jetnet_timer_mgr_t*mgr){
	int new_cap = mgr->trigger_cap<<1;
	jetnet_timer_item_t** new_heap = (jetnet_timer_item_t**)jetnet_malloc(sizeof(jetnet_timer_item_t*)*new_cap);
	memcpy(new_heap,mgr->trigger_heap,sizeof(jetnet_timer_item_t*)*mgr->trigger_cap);
	jetnet_free(mgr->trigger_heap);
	mgr->trigger_cap = new_cap;
	mgr->trigger_heap = new_heap;
}

static void jetnet_timer_heap_adjust(jetnet_timer_mgr_t*mgr, int index) {
	int left_index = JETNET_HEAP_LEFT_CHILD(index);
	int right_index = JETNET_HEAP_RIGHT_CHILD(index);
	int least = index;
	if (left_index <= (mgr->heap_size-1) && JETNET_HEAP_LESS_THAN(mgr,left_index,least)) {
		least = left_index;
	}
	if (right_index <= (mgr->heap_size-1) && JETNET_HEAP_LESS_THAN(mgr,right_index,least)) {
		least = right_index;
	}
	if (least == index) {
		return;
	} else {
		JETNET_HEAP_SWAP(mgr,index,least);
		jetnet_timer_heap_adjust(mgr, least);
	}
}

static void jetnet_timer_heap_add(jetnet_timer_mgr_t*mgr, jetnet_timer_item_t*item){
	if(mgr->heap_size >= mgr->heap_cap)
		jetnet_timer_heap_expand(mgr);
	int index = 0;
	int parent_index = 0;	
	JETNET_HEAP_SET_ITEM(mgr,item,mgr->heap_size);
	index = mgr->heap_size;
	mgr->heap_size++;
	while (index) {
		parent_index = JETNET_HEAP_PARENT(index);
		if(JETNET_HEAP_LESS_THAN(mgr,index,parent_index)){
			JETNET_HEAP_SWAP(mgr,index,parent_index);
		}else{
			break;
		}
		index = parent_index;
	}
}

static jetnet_timer_item_t* jetnet_timer_heap_pop(jetnet_timer_mgr_t*mgr, int index){
	jetnet_timer_item_t*ret = NULL;
	if(index < 0 || index >= mgr->heap_size)
		return ret;
	ret = mgr->timer_heap[index];
	if(index == mgr->heap_size-1){		
		mgr->heap_size--;
	}else{
		JETNET_HEAP_SET_ITEM(mgr,mgr->timer_heap[mgr->heap_size-1],index);
		mgr->heap_size--;
		jetnet_timer_heap_adjust(mgr, index);
	}
	return ret;
}

int jetnet_timer_mgr_init(){
	if(g_timer_mgr)
		return 0;
	g_timer_mgr = (jetnet_timer_mgr_t*)jetnet_malloc(sizeof(jetnet_timer_mgr_t));
	g_timer_mgr->heap_cap = JETNET_TIMER_HEAP_SIZE;
	g_timer_mgr->heap_size = 0;
	g_timer_mgr->timer_heap = (jetnet_timer_item_t**)jetnet_malloc(sizeof(jetnet_timer_item_t*)*g_timer_mgr->heap_cap);
 	memset(g_timer_mgr->timer_heap,0,sizeof(jetnet_timer_item_t*)*g_timer_mgr->heap_cap);
	
	g_timer_mgr->trigger_cap = JETNET_TIMER_TRIGGER_HEAP_SIZE;
	g_timer_mgr->trigger_size = 0;
	g_timer_mgr->trigger_heap = (jetnet_timer_item_t**)jetnet_malloc(sizeof(jetnet_timer_item_t*)*g_timer_mgr->trigger_cap);
	memset(g_timer_mgr->trigger_heap,0,sizeof(jetnet_timer_item_t*)*g_timer_mgr->trigger_cap);
	
	g_timer_mgr->troot = RB_ROOT;
	g_timer_mgr->timer_id_counter = 0;
	return 1;
}

void jetnet_timer_mgr_release(){
	if(!g_timer_mgr)
		return;
	int i;
	for(i=0; i < g_timer_mgr->heap_size; i++)
		jetnet_free(g_timer_mgr->timer_heap[i]);
	jetnet_free(g_timer_mgr->timer_heap);
	
	for(i=0; i < g_timer_mgr->trigger_size; i++)
		jetnet_free(g_timer_mgr->trigger_heap[i]);
	jetnet_free(g_timer_mgr->trigger_heap);
	
	jetnet_timer_free_tree_node(g_timer_mgr->troot.rb_node);
	
	jetnet_free(g_timer_mgr);
	
	g_timer_mgr = NULL;
}

void jetnet_timer_mgr_update(){
	if(!g_timer_mgr)
		return;
	unsigned long now = jetnet_time_now();
	g_timer_mgr->trigger_size = 0;
	while(g_timer_mgr->heap_size > 0){
		if(g_timer_mgr->timer_heap[0]->expire > now)
			break;
		jetnet_timer_item_t* ret = jetnet_timer_heap_pop(g_timer_mgr,0);
		if(!ret)
			break;
		jetnet_timer_trigger_heap_expand(g_timer_mgr);
		g_timer_mgr->trigger_heap[g_timer_mgr->trigger_size++] = ret;
	}
	jetnet_timer_item_t* it;
	for(;g_timer_mgr->trigger_size;g_timer_mgr->trigger_size--){
		it = g_timer_mgr->trigger_heap[g_timer_mgr->trigger_size-1];
		if(it->cb == NULL){
			jetnet_free(it);
			continue;
		}
		if(it->delay > 0){
			it->delay = 0;
			it->expire = now + it->period;
			jetnet_timer_heap_add(g_timer_mgr,it);
			continue;
		}
		it->cb(it->timer_id,it->data,it->ud);
		if(it->cb == NULL){
			jetnet_free(it);
			continue;
		}
		if(it->repeat == 0){
			it->expire = now + it->period;
			jetnet_timer_heap_add(g_timer_mgr,it);
		}else if(it->repeat == 1){
			jetnet_timer_id_node_t*id_node = jetnet_timer_find_tree_node(&g_timer_mgr->troot,it->timer_id);
			if(id_node){
				rb_erase(&id_node->node, &g_timer_mgr->troot);
				jetnet_free(id_node);
			}
			jetnet_free(it);
		}else{
			it->repeat--;
			it->expire = now + it->period;
			jetnet_timer_heap_add(g_timer_mgr,it);						
		}
	}
}

unsigned int jetnet_timer_create(int delay,int period, int repeat, jetnet_timer_cb cb, void*data, void*ud){
	if(!g_timer_mgr)
		return 0;
	jetnet_timer_item_t*item = (jetnet_timer_item_t*)jetnet_malloc(sizeof(jetnet_timer_item_t));
	while(!(item->timer_id = ++g_timer_mgr->timer_id_counter));
	item->cb = cb;
	item->data = data;
	item->ud = ud;
	item->delay = delay;
	item->period = period;
	item->repeat = repeat;	
	if(delay > 0)
		item->expire = jetnet_time_now() + delay;
	else
		item->expire = jetnet_time_now() + period;
	
	jetnet_timer_id_node_t*node = (jetnet_timer_id_node_t*)jetnet_malloc(sizeof(jetnet_timer_id_node_t));
	node->it = item;
	jetnet_timer_add_tree_node(&g_timer_mgr->troot, node);
	
	jetnet_timer_heap_add(g_timer_mgr,item);
	return item->timer_id;
}

void jetnet_timer_kill(unsigned int id){
	if(!g_timer_mgr)
		return;
	jetnet_timer_id_node_t*id_node = jetnet_timer_find_tree_node(&g_timer_mgr->troot,id);
	if(!id_node)
		return;
	id_node->it->cb = NULL;
	rb_erase(&id_node->node, &g_timer_mgr->troot);
	jetnet_free(id_node);
}


