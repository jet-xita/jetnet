CC = gcc
CXX = g++
DEBUG = -g -O0
CFLAGS = $(DEBUG) -Wall -c
RM = rm -rf
SPATH = ../jetnet-src/
HTTP_PATH = ../3rd/http-parser/
PCN = $(SPATH)channel/
PCM = $(SPATH)common/
PEP = $(SPATH)export/
PNT = $(SPATH)net/
PUT = $(SPATH)ultil/
P3D = ../3rd/
PXL = $(P3D)ezxml/
PHT = $(P3D)http-parser/

SRCS = $(PXL)ezxml.c $(PHT)http_parser.c $(PCN)jetnet_sfifo.c\
 $(PCN)jetnet_kfifo.c $(PCN)jetnet_cns.c $(PCM)jetnet_cell_cfg.c\
 $(PCM)jetnet_daemon.c $(PCM)jetnet_errno.c $(PCM)jetnet_ewrapper.c\
 $(PCM)jetnet_malloc.c $(PCM)jetnet_module.c $(PCM)jetnet_mq.c\
 $(PCM)jetnet_time.c $(PCM)jetnet_timer.c $(PUT)jetnet_ultil.c\
 $(PNT)jetnet_http.c $(PNT)jetnet_iobuf.c $(PNT)jetnet_ns.c\
 $(PNT)jetnet_ss.c $(PNT)jetnet_tcppkg.c $(PNT)jetnet_tcpsac.c\
 $(PEP)rbtree.c $(SPATH)jetnet_server.c $(SPATH)jetnet.c

OBJS = $(patsubst %.c, %.o, $(SRCS))

TARGET = jetnet

$(TARGET) : $(OBJS)
	$(CC) $(DEBUG) $^ -o $@ -ldl -rdynamic -I$(PEP) -I$(PXL) -I$(PHT) -I$(PCN) -I$(PCM) -I$(PNT) -I$(PUT)

$(OBJS):%.o : %.c
	$(CC) $(CFLAGS) $< -o $@ -I$(PEP) -I$(PXL) -I$(PHT) -I$(PCN) -I$(PCM) -I$(PNT) -I$(PUT)
	
clean:
	$(RM) jetnet $(OBJS)
	
