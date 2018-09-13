#ifndef JETNET_TIME_H_INCLUDED
#define JETNET_TIME_H_INCLUDED

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * get the current time in microsecond.
 */
extern unsigned long jetnet_time_now();

/**
 * invalid the current time,so it will retrieve from the os.
 */
extern void jetnet_time_invalid();

#if defined(__cplusplus)
}
#endif

#endif /* JETNET_TIME_H_INCLUDED */