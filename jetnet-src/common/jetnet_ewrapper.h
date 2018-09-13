#ifndef __JETNET_ENTITY_WRAPPER_H_INCLUDED
#define __JETNET_ENTITY_WRAPPER_H_INCLUDED

#include <stdbool.h>
#include "jetnet_api.h"
#include "jetnet_msg.h"

#if defined(__cplusplus)
extern "C" {
#endif

//init the entity module
int jetnet_entity_mgr_init(uint32_t cip, uint16_t cport, uint16_t cno);
//release the entity module
void jetnet_entity_mgr_release();
//create an new entity object
jetnet_entity_t* jetnet_entity_create(uint32_t eno);
//find an entity object by entity no
jetnet_entity_t* jetnet_entity_find(uint32_t eno);
//destroy an entity object
void jetnet_entity_destory(jetnet_entity_t*entity);
//push a message to the entity system,the return 
//value indicate whether the caller need to delete the msg
int jetnet_entity_push_msg(jetnet_msg_t*msg);
//pop an active message from entity system
int jetnet_entity_pop_msg(jetnet_msg_t**msg, jetnet_entity_t**entity);

//wait message
unsigned int jetnet_entity_wait_msg(int mode, jetnet_entity_t*entity, jetnet_eid_t src_eid,
		uint32_t msg_seq_id, int time_out, jetnet_wait_msg_cb cb);

void	handle_wait_trigger_msg(jetnet_msg_t*msg);

#if defined(__cplusplus)
}
#endif

#endif /* __JETNET_ENTITY_WRAPPER_H_INCLUDED */
