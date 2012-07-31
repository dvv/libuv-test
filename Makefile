####

PATH := .:$(PATH)

GET := $(shell which curl && echo ' -L --progress-bar')
ifeq ($(GET),)
GET := $(shell which wget && echo ' -q --progress=bar --no-check-certificate -O -')
endif
ifeq ($(GET),)
GET := curl-or-wget-is-missing
endif

####

UV_DIR    := build/libuv

####

CC        := $(CROSS)gcc
LD        := $(CROSS)ld
AR        := $(CROSS)ar

CFLAGS    += -g -pipe -fPIC -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64
LDFLAGS   += 

INCS      := -I$(UV_DIR)/include

LIBS      := $(UV_DIR)/uv.a

all: deps luv

DEPS  := \
  $(UV_DIR)/uv.a

deps: $(DEPS)

#####################
#
# libuv
#
#####################

$(UV_DIR)/uv.a: $(UV_DIR)
	$(MAKE) CC='$(CC)' AR='$(AR)' CFLAGS='$(CFLAGS)' -j 8 -C $^ uv.a

$(UV_DIR):
	mkdir -p build
	$(GET) https://github.com/joyent/libuv/tarball/master | tar -xzpf - -C build
	mv build/joyent-libuv* $@

#####################
#
# luv
#
#####################

luv: src/test-tcp.c src/tcp.c $(LIBS)
	$(CC) -DNODEBUG $(CFLAGS) $(INCS) -o $@ $^ $(LDFLAGS) -lpthread -lm -lrt
	#nemiver ./luv
	#valgrind --leak-check=full --show-reachable=yes -v ./luv
	#sudo chpst -o 2048 ./luv

.PHONY: all deps luv
#.SILENT:
