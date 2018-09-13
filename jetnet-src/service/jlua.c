#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "jetnet_api.h"
#include "rbtree.h"
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"


#define API_EXP __attribute__((visibility("default")))

#define JLUA_TABLE_CHECK(n)\
	do{\
		if(!lua_istable(L,-1)){\
			lua_pop(L,n);\
			lua_pushinteger(L,0);\
			return 1;\
		}\
	}while(0)

#define JLUA_INT_CHECK(n)\
	do{\
		if(!lua_isinteger(L,-1)){\
			lua_pop(L,n);\
			lua_pushinteger(L,0);\
			return 1;\
		}\
	}while(0)

#define JLUA_STRING_CHECK(n)\
	do{\
		if(!lua_isstring(L,-1)){\
			lua_pop(L,n);\
			lua_pushinteger(L,0);\
			return 1;\
		}\
	}while(0)
		
static jetnet_api_t* g_api = NULL;
static lua_State *L = NULL;

int  jlua_msg_cb(jetnet_entity_t*entity, jetnet_msg_t*msg);
void jlua_udfree_cb(jetnet_entity_t*entity);
void jlua_wait_msg_cb(unsigned int wait_id, jetnet_msg_t*msg);


/*
*param:
*return：
*cell_info@table
*/
static int jcapi_get_cell_info(lua_State* L){
	if(!g_api){
		lua_pushnil(L);
		return 1;
	}
	lua_newtable(L);
	int32_t index =	lua_gettop(L);
	lua_pushstring(L,"cell_name");
	lua_pushstring(L,g_api->cell_name);	
	lua_settable(L,index);
	lua_pushstring(L,"cip");
	lua_pushinteger(L,g_api->cip);
	lua_settable(L,index);	
	lua_pushstring(L,"cport");
	lua_pushinteger(L,g_api->cport);
	lua_settable(L,index);	
	lua_pushstring(L,"cno");
	lua_pushinteger(L,g_api->cno);
	lua_settable(L,index);		
	lua_pushstring(L,"hub_cno");
	lua_pushinteger(L,g_api->hub_cno);
	lua_settable(L,index);		
	lua_pushstring(L,"hub_eno");
	lua_pushinteger(L,g_api->hub_eno);
	lua_settable(L,index);
	return 1;
}

/*
*param:
*module_name@string
*return：
*optcode@integer 1:succeed 0:fail
*/
static int jcapi_open_module(lua_State* L){
	if(!g_api){
		lua_pushinteger(L, 0);
		return 1;
	}
	const char*module_name = lua_tostring(L, -1);
	int ret = g_api->open_module(module_name);
	if(!ret){
		lua_pushinteger(L, 0);
		return 1;
	}
	lua_pushinteger(L, 1);
	return 1;	
}

/*
*param:
*return：
*seq_id@integer
*/
static int jcapi_gen_seq_id(lua_State* L){
	if(!g_api){
		lua_pushinteger(L, 0);
		return 1;
	}
	uint32_t ret = g_api->gen_seq_id();
	lua_pushinteger(L, ret);
	return 1;	
}

/*
*param:
*eno@integer,suguest eno.
*return：
*eid@table
*/
static int jcapi_create_entity(lua_State* L){
	if(!g_api){
		lua_pushnil(L);
		return 1;
	}
	
	uint32_t eno;
	if(lua_isinteger(L,-1))
		eno = (uint32_t)lua_tointeger(L, -1);
	else
		eno = 0;
	jetnet_entity_t*entity = g_api->create_entity(eno);
	if(!entity){
		lua_pushnil(L);
		return 1;
	}
	lua_newtable(L);
	int32_t index =	lua_gettop(L);
	lua_pushstring(L,"cip");
	lua_pushinteger(L,(lua_Integer)entity->eid.cip);
	lua_settable(L,index);
	lua_pushstring(L,"cport");
	lua_pushinteger(L,(lua_Integer)entity->eid.cport);
	lua_settable(L,index);
	lua_pushstring(L,"cno");
	lua_pushinteger(L,(lua_Integer)entity->eid.cno);
	lua_settable(L,index);
	lua_pushstring(L,"eno");
	lua_pushinteger(L,(lua_Integer)entity->eid.eno);
	lua_settable(L,index);
	
	entity->msg_cb = &jlua_msg_cb;
	entity->ud_free_func = &jlua_udfree_cb;
	entity->ud = NULL;
	return 1;
}

/*
*param:
*eno@integer
*return：
*/
static int jcapi_destroy_entity(lua_State* L){
	if(!g_api)
		return 0;
	uint32_t eno;
	if(lua_isinteger(L,-1)){
		eno = (uint32_t)lua_tointeger(L, -1);
	}else{
		eno = 0;
	}
	if(eno == 0)
		return 0;
	jetnet_entity_t*entity = g_api->find_entity(eno);
	if(!entity)
		return 0;
	g_api->destroy_entity(entity);
	return 0;
}

/*
*param:
*msg@table
*return：
*optcode@integer 1:ok 0:fail
*/
static int jcapi_post_msg(lua_State* L){
	if(!g_api){
		lua_pushinteger(L,0);
		return 1;
	}	
	if(lua_type(L,-1) != LUA_TTABLE ){
		lua_pushinteger(L,0);
		return 1;
	}
	
	jetnet_msg_t msg;	
	int cur_index = 0;
	int msg_index = lua_gettop(L);
	lua_getfield(L,msg_index,"src_eid");
	JLUA_TABLE_CHECK(1);
		cur_index = lua_gettop(L);
		lua_getfield(L,cur_index,"cip");
		JLUA_INT_CHECK(2);		
		msg.src_eid.cip = (uint32_t)lua_tointeger(L,-1);
		lua_pop(L,1);
		lua_getfield(L,cur_index,"cport");
		JLUA_INT_CHECK(2);
		msg.src_eid.cport = (uint16_t)lua_tointeger(L,-1);
		lua_pop(L,1);
		lua_getfield(L,cur_index,"cno");
		JLUA_INT_CHECK(2);
		msg.src_eid.cno = (uint16_t)lua_tointeger(L,-1);
		lua_pop(L,1);
		lua_getfield(L,cur_index,"eno");
		JLUA_INT_CHECK(2);
		msg.src_eid.eno = (uint32_t)lua_tointeger(L,-1);
		lua_pop(L,1);
	lua_pop(L,1);
	
	lua_getfield(L,msg_index,"dst_eid");
	JLUA_TABLE_CHECK(1);
		cur_index = lua_gettop(L);
		lua_getfield(L,cur_index,"cip");
		JLUA_INT_CHECK(2);
		msg.dst_eid.cip = (uint32_t)lua_tointeger(L,-1);
		lua_pop(L,1);
		lua_getfield(L,cur_index,"cport");
		JLUA_INT_CHECK(2);
		msg.dst_eid.cport = (uint16_t)lua_tointeger(L,-1);
		lua_pop(L,1);
		lua_getfield(L,cur_index,"cno");
		JLUA_INT_CHECK(2);
		msg.dst_eid.cno = (uint16_t)lua_tointeger(L,-1);
		lua_pop(L,1);
		lua_getfield(L,cur_index,"eno");
		JLUA_INT_CHECK(2);
		msg.dst_eid.eno = (uint32_t)lua_tointeger(L,-1);
		lua_pop(L,1);
	lua_pop(L,1);
	
	lua_getfield(L,msg_index,"msg_seq_id");
	JLUA_INT_CHECK(1);
	msg.msg_seq_id = (uint32_t)lua_tointeger(L,-1);
	lua_pop(L,1);

	lua_getfield(L,msg_index,"msg_type");
	JLUA_INT_CHECK(1);
	msg.msg_type = (uint16_t)lua_tointeger(L,-1);
	lua_pop(L,1);
	
	if( msg.msg_type == MSGT_SYS_CMD ){
		lua_getfield(L,msg_index,"msg_tag");
		JLUA_INT_CHECK(1);
		msg.msg_tag_len = sizeof(int);
		*(int*)msg.msg_tag = (int)lua_tointeger(L,-1);
		lua_pop(L,1);
	}else{
		lua_getfield(L,msg_index,"msg_tag");
		JLUA_STRING_CHECK(1);
		size_t msg_tag_len;
		const char* msg_tag = lua_tolstring(L,-1,&msg_tag_len);
		if(msg_tag_len > JETNET_MSG_TAG_LEN){
			lua_pop(L,1);
			lua_pushinteger(L,0);
			return 1;
		}
		msg.msg_tag_len = (uint8_t)msg_tag_len;
		memcpy(msg.msg_tag,msg_tag,msg_tag_len);
		lua_pop(L,1);		
	}
	lua_getfield(L,msg_index,"msg_data");
	JLUA_STRING_CHECK(1);
	size_t msg_data_len;
	const char* msg_data = lua_tolstring(L,-1,&msg_data_len);
	msg.msg_len = (uint32_t)msg_data_len;
	//we need to handle the string data before pop
	int ret = g_api->post_msg_var(&msg,(unsigned char*)msg_data,(unsigned int)msg_data_len);
	lua_pop(L,1);
	if(ret == 0){
		lua_pushinteger(L,0);
		return 1;
	}
	lua_pushinteger(L,1);
	return 1;
}

/*
*param:
*mode@integer
*eno@integer
*src_eid@table
*seq@integer
*wait_time@integer
*return：
*opt_code@integer 0:fail otherwise ,wait_id
*/
static int jcapi_wait(lua_State* L){
#define JLUA_INT_CHECK(n)\
	do{\
		if(!lua_isinteger(L,-1)){\
			lua_pop(L,n);\
			lua_pushinteger(L,0);\
			return 1;\
		}\
	}while(0)
		
	if(!g_api){
		lua_pushinteger(L,0);
		return 1;
	}
	//type check
	if(!lua_isinteger(L,-5) || !lua_isinteger(L,-4) || !lua_istable(L,-3) 
		|| !lua_isinteger(L,-2) || !lua_isinteger(L,-1)){
		lua_pushinteger(L,0);
		return 1;
	}
	
	int mode;
	uint32_t eno;
	jetnet_eid_t src;
	uint32_t seq;
	int wait_time;
	
	mode = (int)lua_tointeger(L, -5);
	eno = (uint32_t)lua_tointeger(L, -4);
	seq = (uint32_t)lua_tointeger(L, -2);
	wait_time = (int)lua_tointeger(L, -1);
	
	int index = -3;
	lua_getfield(L,index,"cip");
	JLUA_INT_CHECK(1);
	src.cip = (uint32_t)lua_tointeger(L,-1);
	lua_pop(L,1);
	lua_getfield(L,index,"cport");
	JLUA_INT_CHECK(1);
	src.cport = (uint16_t)lua_tointeger(L,-1);
	lua_pop(L,1);
	lua_getfield(L,index,"cno");
	JLUA_INT_CHECK(1);
	src.cno = (uint16_t)lua_tointeger(L,-1);
	lua_pop(L,1);
	lua_getfield(L,index,"eno");
	JLUA_INT_CHECK(1);
	src.eno = (uint32_t)lua_tointeger(L,-1);
	lua_pop(L,1);
	
	jetnet_entity_t*entity = g_api->find_entity(eno);
	if(!entity){
		lua_pushinteger(L,0);
		return 1;
	}
	unsigned int wait_id = g_api->wait(mode,entity,src,seq,wait_time,&jlua_wait_msg_cb);
	lua_pushinteger(L,wait_id);
	return 1;
}

/*
function jcb_msg(msg)
end
*/
static int jlua_nty_msg(jetnet_msg_t*msg){
	if(!g_api)
		return 0;
	
	int index1,index2;
	lua_getglobal(L,"jcb_msg");	
	lua_newtable(L);
	
	index1 = lua_gettop(L);
	lua_pushstring(L,"src_eid");
	lua_newtable(L);
		index2 = lua_gettop(L);	
		lua_pushstring(L,"cip");
		lua_pushinteger(L,msg->src_eid.cip);
		lua_settable(L,index2);	
		lua_pushstring(L,"cport");
		lua_pushinteger(L,msg->src_eid.cport);
		lua_settable(L,index2);
		lua_pushstring(L,"cno");
		lua_pushinteger(L,msg->src_eid.cno);
		lua_settable(L,index2);
		lua_pushstring(L,"eno");
		lua_pushinteger(L,msg->src_eid.eno);
		lua_settable(L,index2);	
	lua_settable(L,index1);
	
	lua_pushstring(L,"dst_eid");
	lua_newtable(L);
		index2 = lua_gettop(L);	
		lua_pushstring(L,"cip");
		lua_pushinteger(L,msg->dst_eid.cip);
		lua_settable(L,index2);	
		lua_pushstring(L,"cport");
		lua_pushinteger(L,msg->dst_eid.cport);
		lua_settable(L,index2);
		lua_pushstring(L,"cno");
		lua_pushinteger(L,msg->dst_eid.cno);
		lua_settable(L,index2);
		lua_pushstring(L,"eno");
		lua_pushinteger(L,msg->dst_eid.eno);
		lua_settable(L,index2);	
	lua_settable(L,index1);
	
	lua_pushstring(L,"msg_seq_id");
	lua_pushinteger(L,msg->msg_seq_id);
	lua_settable(L,index1);

	lua_pushstring(L,"msg_type");
	lua_pushinteger(L,msg->msg_type);
	lua_settable(L,index1);

	if(msg->msg_type == MSGT_SYS_CMD){
		lua_pushstring(L,"msg_tag");
		lua_pushinteger(L,*(int*)msg->msg_tag);
		lua_settable(L,index1);
	}else{
		lua_pushstring(L,"msg_tag");
		lua_pushlstring(L,(const char*)msg->msg_tag,(size_t)msg->msg_tag_len);
		lua_settable(L,index1);
	}
	
	lua_pushstring(L,"msg_data");
	lua_pushlstring(L,(const char*)msg->msg_data,(size_t)msg->msg_len);
	lua_settable(L,index1);
	
	if(lua_pcall(L,1,0,0)!=0){
		printf("call jcb_msg fail:%s\n",lua_tostring(L,-1));
		lua_pop(L,1);
		return 0;
	}
	return 1;
}

/*
function jcb_efree(eno)
end
*/
static void jlua_nty_udfree(uint32_t eno){
	if(!g_api)
		return;	
	lua_getglobal(L,"jcb_efree");
	lua_pushinteger(L,eno);
	if(lua_pcall(L,1,0,0)!=0){
		printf("call jlua_nty_udfree fail:%s\n",lua_tostring(L,-1));
		lua_pop(L,1);
		return;
	}
}

/*
function jcb_wait(wait_id,msg)
end
*/
static void jlua_nty_wait_msg(uint32_t wait_id,jetnet_msg_t*msg){
	if(!g_api)
		return;

	lua_getglobal(L,"jcb_wait");
	lua_pushinteger(L,wait_id);
	if(!msg){
		lua_pushnil(L);
		if(lua_pcall(L,2,0,0)!=0){
			printf("call jlua_nty_wait_msg fail:%s\n",lua_tostring(L,-1));
			lua_pop(L,1);
			return;
		}
		return;
	}
	int index1,index2;
	lua_newtable(L);	
	index1 = lua_gettop(L);
	lua_pushstring(L,"src_eid");
	lua_newtable(L);
		index2 = lua_gettop(L);	
		lua_pushstring(L,"cip");
		lua_pushinteger(L,msg->src_eid.cip);
		lua_settable(L,index2);	
		lua_pushstring(L,"cport");
		lua_pushinteger(L,msg->src_eid.cport);
		lua_settable(L,index2);
		lua_pushstring(L,"cno");
		lua_pushinteger(L,msg->src_eid.cno);
		lua_settable(L,index2);
		lua_pushstring(L,"eno");
		lua_pushinteger(L,msg->src_eid.eno);
		lua_settable(L,index2);	
	lua_settable(L,index1);
	
	lua_pushstring(L,"dst_eid");
	lua_newtable(L);
		index2 = lua_gettop(L);	
		lua_pushstring(L,"cip");
		lua_pushinteger(L,msg->dst_eid.cip);
		lua_settable(L,index2);	
		lua_pushstring(L,"cport");
		lua_pushinteger(L,msg->dst_eid.cport);
		lua_settable(L,index2);
		lua_pushstring(L,"cno");
		lua_pushinteger(L,msg->dst_eid.cno);
		lua_settable(L,index2);
		lua_pushstring(L,"eno");
		lua_pushinteger(L,msg->dst_eid.eno);
		lua_settable(L,index2);	
	lua_settable(L,index1);
	
	lua_pushstring(L,"msg_seq_id");
	lua_pushinteger(L,msg->msg_seq_id);
	lua_settable(L,index1);

	lua_pushstring(L,"msg_type");
	lua_pushinteger(L,msg->msg_type);
	lua_settable(L,index1);

	lua_pushstring(L,"msg_tag");
	lua_pushlstring(L,(const char*)msg->msg_tag,(size_t)msg->msg_tag_len);
	lua_settable(L,index1);
	
	lua_pushstring(L,"msg_data");
	lua_pushlstring(L,(const char*)msg->msg_data,(size_t)msg->msg_len);
	lua_settable(L,index1);
	
	if(lua_pcall(L,2,0,0)!=0){
		printf("call jlua_nty_wait_msg fail:%s\n",lua_tostring(L,-1));
		lua_pop(L,1);
		return;
	}
}

int  jlua_msg_cb(jetnet_entity_t*entity, jetnet_msg_t*msg){
	return jlua_nty_msg(msg);	
}

void jlua_udfree_cb(jetnet_entity_t*entity){
	jlua_nty_udfree(entity->eid.eno);
}

void jlua_wait_msg_cb(unsigned int wait_id, jetnet_msg_t*msg){
	jlua_nty_wait_msg(wait_id,msg);
}

API_EXP int jlua_init(jetnet_api_t*api){
	if(g_api)
		return 0;
	g_api = api;
    L = luaL_newstate();
    luaL_openlibs(L);
	//registe function
	lua_register(L, "jcapi_get_cell_info", jcapi_get_cell_info);
	lua_register(L, "jcapi_open_module", jcapi_open_module);
	lua_register(L, "jcapi_gen_seq_id", jcapi_gen_seq_id);	
	lua_register(L, "jcapi_create_entity", jcapi_create_entity);
	lua_register(L, "jcapi_destroy_entity", jcapi_destroy_entity);
	lua_register(L, "jcapi_post_msg", jcapi_post_msg);
	lua_register(L, "jcapi_wait", jcapi_wait);
	//setup message type variable
	lua_pushinteger(L,MSGT_TRANS_LUA);
	lua_setglobal(L,"MSGT_TRANS_LUA");
	lua_pushinteger(L,MSGT_LUA_RPC_REQ);
	lua_setglobal(L,"MSGT_LUA_RPC_REQ");
	lua_pushinteger(L,MSGT_LUA_RPC_ACK);
	lua_setglobal(L,"MSGT_LUA_RPC_ACK");
	lua_pushinteger(L,MSGT_SYS_CMD);
	lua_setglobal(L,"MSGT_SYS_CMD");
	
	//setup command id
	lua_pushinteger(L,CMDTAG_CONNECT);
	lua_setglobal(L,"CMDTAG_CONNECT");
	lua_pushinteger(L,CMDTAG_ACCEPT);
	lua_setglobal(L,"CMDTAG_ACCEPT");
	lua_pushinteger(L,CMDTAG_RECV);
	lua_setglobal(L,"CMDTAG_RECV");
	lua_pushinteger(L,CMDTAG_CLOSE);
	lua_setglobal(L,"CMDTAG_CLOSE");
	lua_pushinteger(L,CMDTAG_UNREACH);
	lua_setglobal(L,"CMDTAG_UNREACH");
	lua_pushinteger(L,CMDTAG_RAW);
	lua_setglobal(L,"CMDTAG_RAW");
	lua_pushinteger(L,CMDTAG_DELIVER);
	lua_setglobal(L,"CMDTAG_DELIVER");
	lua_pushinteger(L,CMDTAG_SNDMSG);
	lua_setglobal(L,"CMDTAG_SNDMSG");
	lua_pushinteger(L,CMDTAG_COMMON);
	lua_setglobal(L,"CMDTAG_COMMON");
	//setup the wait mode
	lua_pushinteger(L,JETNET_WAIT_MODE_SYNC);
	lua_setglobal(L,"JETNET_WAIT_MODE_SYNC");
	lua_pushinteger(L,JETNET_WAIT_MODE_ASYNC);
	lua_setglobal(L,"JETNET_WAIT_MODE_ASYNC");
	
	//load script
	if(luaL_dofile(L, "./script/main.lua")){
		printf("load script file fail:\t%s\n",lua_tostring(L,-1));
		
		lua_close(L);
		L = NULL;
		g_api = NULL;
		return 0;
	}
	//call init
	lua_getglobal(L,"jcb_init");
	if(lua_pcall(L,0,1,0)!=0){
		printf("lcb init fail:%s\n",lua_tostring(L,-1));
		lua_pop(L,1);
		
		lua_close(L);
		L = NULL;
		g_api = NULL;
		return 0;
	}
	int ret	= lua_toboolean(L,-1);
	lua_pop(L,1);
	return ret;
}

API_EXP void jlua_startup(jetnet_api_t*api){
	lua_getglobal(L,"jcb_start");
	if(lua_pcall(L,0,0,0)!=0){
		printf("lcb init fail:%s\n",lua_tostring(L,-1));
		lua_pop(L,1);
	}
}

API_EXP void jlua_release(jetnet_api_t*api){
	if(!g_api)
		return;
	lua_close(L);
	L = NULL;
	g_api = NULL;
}


