CFG_TEE_TA_LOG_LEVEL ?= 4
CPPFLAGS += -DCFG_TEE_TA_LOG_LEVEL=$(CFG_TEE_TA_LOG_LEVEL)

# use our own mbedTLS configuration
CFLAGS+=	-I../external/ -DMBEDTLS_CONFIG_FILE='<bstgw_mbed_config.h>'
CPPFLAGS+=	-I../external/ -DMBEDTLS_CONFIG_FILE='<bstgw_mbed_config.h>'

# The UUID for the Trusted Application
BINARY=0b9d11c7-2281-4b24-b5f0-6c15e10885fb

-include $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk

ifeq ($(wildcard $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk), )
clean:
	@echo 'Note: $$(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk not found, cannot clean TA'
	@echo 'Note: TA_DEV_KIT_DIR=$(TA_DEV_KIT_DIR)'
endif
