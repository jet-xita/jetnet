CC = gcc
CXX = g++
DEBUG = -g -O0
CFLAGS = $(DEBUG) -Wall -c
SHARE = -fPIC -shared
RM = rm -rf
PATH_EXP = ../export/
PATH_LUA_EXP = ../../3rd/lua5.3.4/export/
PATH_LUA_LIB = ../../3rd/lua5.3.4/lib/

all : gate.so huber.so jlua.so

.PHONY : all

huber.so : huber.o
	$(CC) $(SHARE) $(DEBUG) -fvisibility=hidden huber.o -o huber.so
 
huber.o:huber.c 
	$(CC) $(CFLAGS) $(SHARE) huber.c -I$(PATH_EXP) -o huber.o
	
gate.so : gate.o
	$(CC) $(SHARE) $(DEBUG) -fvisibility=hidden gate.o -o gate.so
 
gate.o:gate.c 
	$(CC) $(CFLAGS) $(SHARE) gate.c -I$(PATH_EXP) -o gate.o

jlua.so : jlua.o
	$(CC) $(SHARE) $(DEBUG) -L$(PATH_LUA_LIB) -fvisibility=hidden jlua.o -Wl,-Bstatic -llua -Wl,-Bdynamic -ldl -lm -o jlua.so
 
jlua.o:jlua.c 
	$(CC) $(CFLAGS) $(SHARE) jlua.c -I$(PATH_EXP) -I$(PATH_LUA_EXP) -o jlua.o
	
clean:
	$(RM) huber.o huber.so gate.o gate.so jlua.o jlua.so
 
	
