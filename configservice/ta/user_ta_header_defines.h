/*
 * The name of this file must not be modified
 */

#ifndef USER_TA_HEADER_DEFINES_H
#define USER_TA_HEADER_DEFINES_H

/* To get the TA UUID definition */
#include <bstgw/web_stub_ta.h>

#define TA_UUID				BSTGW_TA_WEB_STUB_UUID

/*
 * TA properties: multi-instance TA, no specific attribute
 * TA_FLAG_EXEC_DDR is meaningless but mandated.
 */
#define TA_FLAGS		    ( TA_FLAG_SINGLE_INSTANCE | TA_FLAG_MULTI_SESSION )

/* Provisioned stack size */
#define TA_STACK_SIZE			(2 * 1024)

/* Provisioned heap size for TEE_Malloc() and friends */
#define TA_DATA_SIZE			(128 * 1024) // TODO: default is (32 * 1024)

/* The gpd.ta.version property */
#define TA_VERSION	"0.1"

/* The gpd.ta.description property */
#define TA_DESCRIPTION	"ConfigService TA ++ integrated WebStub for trusted configuration of the TrustedGateway"

#endif /* USER_TA_HEADER_DEFINES_H */
