#ifndef JETNET_TIMER_H_INCLUDED
#define JETNET_TIMER_H_INCLUDED

#include "jetnet_api.h"

#if defined(__cplusplus)
extern "C" {
#endif

int	jetnet_timer_mgr_init();
void	jetnet_timer_mgr_release();
void	jetnet_timer_mgr_update();

unsigned int jetnet_timer_create(int delay,int period, int repeat, jetnet_timer_cb cb, void*data, void*ud);
void jetnet_timer_kill(unsigned int id);

#if defined(__cplusplus)
}
#endif

#endif /* JETNET_TIMER_H_INCLUDED */