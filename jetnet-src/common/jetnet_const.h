#ifndef __JETNET_CONST_H_INCLUDED
#define __JETNET_CONST_H_INCLUDED

#if defined(__cplusplus)
extern "C" {
#endif

/*define the max module type*/
#define MAX_MODULE_TYPE 32

/*the max length of service name*/
#define	JETNET_CELL_NAME_LEN 16

/*the max length of service name*/
#define	JETNET_SERVER_NAME_LEN 16

/*define the entity msg wait type*/
#define	WAIT_TYPE_SRCNSEQ 0 /*filter the src_entity_id and the msg_seq_id*/
#define	WAIT_TYPE_SEQ 1 /*filter the the msg_seq_id onley*/

#define INVALID_WAIT_ID	0

/*define the max socket address size*/
#define ADDRESS_SIZE 17

#define JETNET_INVALID_ENO	0

#if defined(__cplusplus)
}
#endif

#endif /* __JETNET_CONST_H_INCLUDED */