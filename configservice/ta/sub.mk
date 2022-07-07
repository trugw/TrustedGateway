global-incdirs-y += include
srcs-y += web_stub_ta.c

incdirs-y += .
srcs-y += sw_mbedtls_util.c
srcs-y += parser_utils.c
srcs-y += secure_ta.c
srcs-y += sw_crt_mng.c
srcs-y += sw_fileio.c
srcs-y += ringbuffer.c
srcs-y += response_queue.c
#srcs-y += config_proxy.c

incdirs-y += ../external/picohttpparser
srcs-y += ../external/picohttpparser/picohttpparser.c

cflags-ringbuffer.c-y += -DBSTGW_TA=1

#NOTE: requires CFG_TA_MBEDTLS in OP-TEE which will auto-link the mbedTLS UTA
#      library against every user TA

# To remove a certain compiler flag, add a line like this
#cflags-template_ta.c-y += -Wno-strict-prototypes
