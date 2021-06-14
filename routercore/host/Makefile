CC      ?= $(CROSS_COMPILE)gcc
LD      ?= $(CROSS_COMPILE)ld
AR      ?= $(CROSS_COMPILE)ar
NM      ?= $(CROSS_COMPILE)nm
OBJCOPY ?= $(CROSS_COMPILE)objcopy
OBJDUMP ?= $(CROSS_COMPILE)objdump
READELF ?= $(CROSS_COMPILE)readelf

OBJS = router_main.o router.o nw_cleaner.o nw_worker.o nw_router_thread.o router_stats.o

CFLAGS += -Wall -I../ta/include -I$(TEEC_EXPORT)/include -I.

#Add/link other required libraries here
LDADD += -lteec -L$(TEEC_EXPORT)/lib
LDADD += -pthread

BINARY = optee_example_router_helper

.PHONY: all
all: $(BINARY)

$(BINARY): $(OBJS)
	$(CC) $^ -o $@ $(LDADD)

.PHONY: clean
clean:
	rm -f $(OBJS) $(BINARY)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
