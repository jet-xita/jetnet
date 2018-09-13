#ifndef JETNET_HTTP_H_INCLUDED
#define JETNET_HTTP_H_INCLUDED

#include "jetnet_proto.h"

#if defined(__cplusplus)
extern "C" {
#endif


jetnet_proto_t* jetnet_http_create();
void jetnet_http_release();

//define the header field's key & value parse result
typedef struct jetnet_http_field_t{
	size_t key_offset;
	size_t key_len;
	size_t value_offset;
	size_t value_len;	
}jetnet_http_field_t;

//define the http unpack format struct
typedef struct jetnet_http_data_t{
	bool get_header_complete;
	bool get_message_complete;
	bool be_request;
	bool should_keep_alive;
	unsigned short http_major;
	unsigned short http_minor;
	unsigned int method;
	size_t url_offset;
	size_t url_len;
	unsigned int status_code;	
	size_t body_offset;
	size_t body_len;
	size_t http_data_size;
	unsigned int field_num;
	jetnet_http_field_t	field_infos[1];
}jetnet_http_data_t;

#define JETNET_HTTP_HEADER_FIELDS_OFFSET (offsetof(jetnet_http_data_t, field_infos[0]))
char* get_http_real_data(jetnet_http_data_t*data);

void dump_http_data(jetnet_http_data_t*data);

#if defined(__cplusplus)
}
#endif

#endif /* JETNET_HTTP_H_INCLUDED */