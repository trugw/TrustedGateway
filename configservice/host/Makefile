CC      ?= $(CROSS_COMPILE)gcc
LD      ?= $(CROSS_COMPILE)ld
AR      ?= $(CROSS_COMPILE)ar
NM      ?= $(CROSS_COMPILE)nm
OBJCOPY ?= $(CROSS_COMPILE)objcopy
OBJDUMP ?= $(CROSS_COMPILE)objdump
READELF ?= $(CROSS_COMPILE)readelf

OBJS = server_main.o server.o client.o ringbuffer.o shm_rb.o webapp_loading.o

CFLAGS += -Wall -I../ta/include -I$(TEEC_EXPORT)/include -I.
# use our own mbedTLS configuration
CFLAGS += -I../external/ -DMBEDTLS_CONFIG_FILE='<bstgw_mbed_config.h>'

#Add/link other required libraries here
LDADD += -lteec -L$(TEEC_EXPORT)/lib
LDADD += -pthread

#BINARY = bstgw_sock_helper
BINARY = optee_example_sock_helper

.PHONY: all
all: $(BINARY)

$(BINARY): $(OBJS)
	$(CC) $^ -o $@ $(LDADD)

.PHONY: clean
clean:
	rm -f $(OBJS) $(BINARY)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
