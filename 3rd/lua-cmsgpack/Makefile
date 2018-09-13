CC = gcc
CXX = g++
DEBUG = -g -O2
CFLAGS = $(DEBUG) -Wall -c -ggdb -pedantic -std=c99
SHARE = -fPIC -shared
RM = rm -rf

PATH_LUA_EXP = ../../3rd/lua5.3.4/export/
PATH_LUA_LIB = ../../3rd/lua5.3.4/lib/

all : cmsgpack.so

.PHONY : all

cmsgpack.so : cmsgpack.o
	$(CC) $(SHARE) $(DEBUG) -L$(PATH_LUA_LIB) -fvisibility=hidden cmsgpack.o -Wl,-Bstatic -llua -Wl,-Bdynamic -ldl -lm -o cmsgpack.so
 
cmsgpack.o:lua_cmsgpack.c
	$(CC) $(CFLAGS) $(SHARE) lua_cmsgpack.c -I$(PATH_LUA_EXP) -o cmsgpack.o
	
clean:
	$(RM) cmsgpack.o cmsgpack.so