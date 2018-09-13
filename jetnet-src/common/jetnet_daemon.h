#ifndef JETNET_DAEMON_H_INCLUDE
#define JETNET_DAEMON_H_INCLUDE

int daemon_init(const char *pidfile);
int daemon_exit(const char *pidfile);

#endif /*JETNET_DAEMON_H_INCLUDE*/
