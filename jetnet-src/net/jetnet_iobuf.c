#include<stdlib.h>
#include<string.h>
#include "jetnet_iobuf.h"
#include "jetnet_malloc.h"

void jetnet_rbl_free(jetnet_rbl_t *rbl) {
	jetnet_rb_t *rb = rbl->head;
	while (rb) {
		jetnet_rb_t *tmp = rb;
		rb = rb->next;
		jetnet_rb_free(tmp);
	}
	rbl->head = NULL;
	rbl->tail = NULL;
	rbl->offset = 0;
	rbl->size = 0;
}

void jetnet_rbl_appen(jetnet_rbl_t *rbl,jetnet_rb_t*rb) {
	rb->next = NULL;
	if (rbl->head == NULL) {
		rbl->head = rbl->tail = rb;
		rbl->offset = 0;
		rbl->size = rb->size;
	} else {
		rbl->tail->next = rb;
		rbl->tail = rb;
		rbl->size += rb->size;
	}
}

int jetnet_rbl_read(jetnet_rbl_t *rbl, char*data, int len){
	int copy_size = 0;
	int cur_size = 0;
	jetnet_rb_t*temp;
	while(rbl->head){
		cur_size = rbl->head->size - rbl->offset;
		if(cur_size <= 0){
			temp = rbl->head;
			rbl->offset -=  rbl->head->size;
			rbl->head = rbl->head->next;
			if(rbl->head == NULL)
				rbl->tail = NULL;
			jetnet_rb_free(temp);
		}else{
			if(copy_size >= len)
				break;
			cur_size = cur_size <= len - copy_size ? cur_size : len - copy_size;
			memcpy((char*)data+copy_size,(char*)rbl->head->buffer+rbl->offset,cur_size);
			copy_size += cur_size;
			rbl->offset += cur_size;
			rbl->size -= cur_size;
		}		
	}
	return copy_size;
}

int jetnet_rbl_peek(jetnet_rbl_t *rbl, int offset, char*buf, int size){
	if(rbl->size < offset + size)
		return -1;
	int n1 = 0;
	int n2;
	int n3 = offset + rbl->offset;
	int n4 = n3 + size - 1;
	char* c;
	int cur = 0;
	jetnet_rb_t*cur_buf = rbl->head;
	while(cur_buf){
		n2 = n1 + cur_buf->size - 1;
		if(n2 >= n3){
			c = (char*)cur_buf->buffer + n3 - n1;
			if(n2 >= n4){
				while(n3 <= n4){
					buf[cur++] = *(c++);
					n3++;
				}
				break;
			}else{
				while(n3 <= n2){
					buf[cur++] = *(c++);
					n3++;
				}
			}
		}		
		n1 += cur_buf->size;
		cur_buf = cur_buf->next;
	}
	return 0;
}

//helper function for write buffer
void jetnet_wb_free(jetnet_wb_t *wb){
	int i;
	for(i = 0; i < wb->vec_count; i++){
		jetnet_free((void*)wb->vec[i].iov_base);
	}
	jetnet_free((void*)wb);
}

void jetnet_wbl_free(jetnet_wbl_t *wbl) {
	jetnet_wb_t *wb = wbl->head;
	while (wb) {
		jetnet_wb_t *tmp = wb;
		wb = wb->next;
		jetnet_wb_free(tmp);
	}
	wbl->head = NULL;
	wbl->tail = NULL;
}

void jetnet_wbl_appen(jetnet_wbl_t *wbl, jetnet_wb_t*wb) {
	wb->next = NULL;
	if (wbl->head == NULL) {
		wbl->head = wbl->tail = wb;
	} else {
		wbl->tail->next = wb;
		wbl->tail = wb;
	}
}

jetnet_wb_t* jetnet_wbl_pop(jetnet_wbl_t *wbl) {
	if (wbl->head == NULL) {
		return NULL;
	} else {
		jetnet_wb_t* ret = wbl->head;
		wbl->head = wbl->head->next;
		if(wbl->head == NULL)
			wbl->tail = NULL;
		return ret;
	}
}

void jetnet_wbl_drop(jetnet_wbl_t *wbl,int size){
	int cursor;
	int cur_wb_size;
	//free the write buffer which had beed sent in this operation
	while(wbl->head && size > 0){
		cur_wb_size = 0;			
		for(cursor = 0;cursor<wbl->head->vec_count;cursor++){
			cur_wb_size += wbl->head->vec[cursor].iov_len;				
		}
		cur_wb_size -= wbl->head->offset;
		if(cur_wb_size > size){
			wbl->head->offset += size;
			break;
		}else{
			size -= cur_wb_size;
			jetnet_wb_t*wb = jetnet_wbl_pop(wbl);
			jetnet_wb_free(wb);			
		}
	}
}

int jetnet_wbl_get_iovec(jetnet_wbl_t *wbl,jetnet_iov_t*vec, int count, int*byte_cnt){
	*byte_cnt = 0;
	int cur = 0;
	int total_size = 0;
	int i , wb_offset;
	jetnet_wb_t* wb;
	for(wb = wbl->head; wb && cur < count ; wb=wb->next){
		wb_offset = wb->offset;
		for(i = 0;i<wb->vec_count;i++){
			if( wb_offset < wb->vec[i].iov_len ){
				vec[cur].iov_base = (char*)wb->vec[i].iov_base + wb_offset;
				vec[cur].iov_len = wb->vec[i].iov_len - wb_offset;
				total_size += vec[cur].iov_len;
				cur++;
				wb_offset = 0;
				if(cur == count)
					break;
			}else{
				wb_offset -= wb->vec[i].iov_len;
			}
		}
	}
	*byte_cnt = total_size;
	return cur;
}

int jetnet_wbl_size(jetnet_wbl_t *wbl){
	int size = 0;
	jetnet_wb_t* wb = wbl->head;
	int i;
	while(wb){
		for(i = 0;i<wb->vec_count;i++)
			size += wb->vec[i].iov_len;
		size -= wb->offset;
		wb = wb->next;
	}
	return size;
}

jetnet_wb_t* jetnet_wbl_pop_head(jetnet_wbl_t *wbl){
	if(wbl->head){
		jetnet_wb_t*temp = wbl->head;
		wbl->head = temp->next;
		if(!wbl->head){
			wbl->tail = NULL;
		}
		temp->next = NULL;
		return temp;
	}
	return NULL;	
}

