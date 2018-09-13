#include <sys/time.h>
#include <time.h>
#include <stdbool.h>
#include "jetnet_time.h"

bool jetnet_time_valid_flag = false;
unsigned long jetnet_current_time = 0;

unsigned long jetnet_time_now(){
	if(jetnet_time_valid_flag)
		return jetnet_current_time;
	
	struct timeval tv;
	gettimeofday(&tv, NULL);
	jetnet_current_time = tv.tv_sec*1000 + tv.tv_usec/1000;
	jetnet_time_valid_flag = true;
	return jetnet_current_time;
}

void jetnet_time_invalid(){
	jetnet_time_valid_flag = false;
}