#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_DEPRECATE)
#define _CRT_SECURE_NO_DEPRECATE 1
#endif

/* Re-ordered to match the config in optee_os for better comparison */
/*
 * When wrapping using TEE_BigInt to represent a mbedtls_mpi we can only
 * use 32-bit arithmetics.
 */
#define MBEDTLS_HAVE_INT32

#define MBEDTLS_BIGNUM_C
#define MBEDTLS_GENPRIME

#define MBEDTLS_MD_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA512_C // includes sha-384
//#define MBEDTLS_SHA256_SMALLER
//#define MBEDTLS_SHA512_SMALLER

// Just to prevent a compilation error in mbedtls_taf.c of crypt optee-test
#define MBEDTLS_SHA1_C
#define MBEDTLS_MD5_C
#define MBEDTLS_DES_C
// other configs just added bcs. they are required by OP-TEE's uta config
//#define MBEDTLS_CIPHER_MODE_CBC
//#define MBEDTLS_ECDSA_C


#define MBEDTLS_AES_C
//#define MBEDTLS_AES_ROM_TABLES // used in core, not UTA
//#define MBEDTLS_AES_FEWER_TABLES
//#define MBEDTLS_AES_ALT  // TODO: conditionally used by OP-TEE

//#define MBEDTLS_CMAC_C  ? (OP-TEE has enabled it)
#define MBEDTLS_CIPHER_C

#define MBEDTLS_OID_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_PK_C
#define MBEDTLS_CTR_DRBG_C

#define MBEDTLS_RSA_C
//#define MBEDTLS_RSA_NO_CRT // only used for core

#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C

#define MBEDTLS_ECP_DP_SECP192R1_ENABLED
#define MBEDTLS_ECP_DP_SECP224R1_ENABLED
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
/*
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_SECP521R1_ENABLED
#define MBEDTLS_ECP_DP_SECP192K1_ENABLED
#define MBEDTLS_ECP_DP_SECP224K1_ENABLED
#define MBEDTLS_ECP_DP_SECP256K1_ENABLED
#define MBEDTLS_ECP_DP_BP256R1_ENABLED
#define MBEDTLS_ECP_DP_BP384R1_ENABLED
#define MBEDTLS_ECP_DP_BP512R1_ENABLED
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED // not supported in TLS1.2, but only TLS1.3?!
#define MBEDTLS_ECP_DP_CURVE448_ENABLED
*/
//#define MBEDTLS_ECDH_LEGACY_CONTEXT // only by core
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECP_C

/* --------------------------------------------------- */


/* - **** Adapted from sample configs **** - */
/* Save RAM at the expense of ROM */
#define MBEDTLS_AES_ROM_TABLES

/*
 * Save RAM at the expense of interoperability: do this only if you control
 * both ends of the connection!  (See comments in "mbedtls/ssl.h".)
 * The optimal size here depends on the typical size of records.
 */
// Note: 1024 was too small w/o additional client-side config, bcs. currnet client cert is already about 1.6-1.7kB
// NOTE(!):  Could maybe reduce (at least for input direction) if switching to EC?
//#define MBEDTLS_SSL_MAX_CONTENT_LEN             1024
//#define MBEDTLS_SSL_MAX_CONTENT_LEN             1792 //2048
#define MBEDTLS_SSL_OUT_CONTENT_LEN               1792


/* Save RAM by adjusting to our exact needs */
#define MBEDTLS_ECP_MAX_BITS   256

// causes weird error when parsing the RSA public key of the server test certificate
//#define MBEDTLS_MPI_MAX_SIZE    32 // 256 bits is 32 bytes

// TODO: we currently have the demo certificates which use RSA 2048 --> 256 Bytes
#define MBEDTLS_MPI_MAX_SIZE 256 // 2048 bits is 256 bytes


/* Save RAM at the expense of speed, see ecp.h */
#define MBEDTLS_ECP_WINDOW_SIZE        2
#define MBEDTLS_ECP_FIXED_POINT_OPTIM  0

/* Significant speed benefit at the expense of some ROM */
// TODO: I think we don't need it in case ROM is getting low
#define MBEDTLS_ECP_NIST_OPTIM
/* - **** Adapted from sample configs **** - */


/* --------------------------------------------------- */

/* The ones that should be undefined according to the porting guide */
//#define MBEDTLS_TIMING_C
//#define MBEDTLS_NET_C
//#define MBEDTLS_ENTROPY_C // that one?
//#define MBEDTLS_FS_IO
//#define MBEDTLS_HAVE_TIME_DATE
//#define MBEDTLS_HAVE_TIME

// like OP-TEE
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_NO_PLATFORM_ENTROPY
/* TODO: this also means we will currently NOT HAVE ANY ENTROPY SOURCE, BUT ONLY THE WEAK DUMMY ONE
 *    that means, either we have to explicilty register a SW-one, use HW_ALT,
 *    ... or at least use NULL_ENTROPY (fully insecure) for testing */

#define MBEDTLS_TEST_NULL_ENTROPY  /* WARNING:  insecure, just for testing till we added source */
#define MBEDTLS_NO_DEFAULT_ENTROPY_SOURCES // pre-req. of NULL_ENTROPY

/* --------------------------------------------------- */

#define MBEDTLS_REMOVE_ARC4_CIPHERSUITES
#define MBEDTLS_REMOVE_3DES_CIPHERSUITES

/**
 *      MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384  (x)
 */
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_SSL_CIPHERSUITES MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384

#define MBEDTLS_GCM_C


#define MBEDTLS_SSL_ALL_ALERT_MESSAGES
#define MBEDTLS_SSL_RECORD_CHECKING
//#define MBEDTLS_SSL_ASYNC_PRIVATE
//#define MBEDTLS_SSL_RENEGOTIATION
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET
#define MBEDTLS_SSL_FALLBACK_SCSV

/* TODO: commenting it out saves RAM! (~1600 Bytes), but causes mbedtls_ssl_get_peer_cert() to return NULL */
#define MBEDTLS_SSL_KEEP_PEER_CERTIFICATE


#define MBEDTLS_SSL_MAX_FRAGMENT_LENGTH

#define MBEDTLS_SSL_PROTO_TLS1_2
// Temporary enable, bcs. Safari always tried to connect via TLS1.0, causing a mbedTLS error (where I close atm)
#define MBEDTLS_SSL_PROTO_TLS1
#define MBEDTLS_SSL_PROTO_TLS1_1


//#define MBEDTLS_SSL_CACHE_C // I think I removed it from our code atm

#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_SRV_C
#define MBEDTLS_SSL_TLS_C

#define MBEDTLS_SSL_ALPN // for HTTPS/2.0

/**
 * \def MBEDTLS_SSL_SESSION_TICKETS
 *
 * Enable support for RFC 5077 session tickets in SSL.
 * Client-side, provides full support for session tickets (maintenance of a
 * session store remains the responsibility of the application, though).
 * Server-side, you also need to provide callbacks for writing and parsing
 * tickets, including authenticated encryption and key management. Example
 * callbacks are provided by MBEDTLS_SSL_TICKET_C.
 *
 * Comment this macro to disable support for SSL session tickets
 */
#define MBEDTLS_SSL_SESSION_TICKETS
#define MBEDTLS_SSL_TICKET_C


#define MBEDTLS_SSL_EXPORT_KEYS // ?

#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_SSL_TRUNCATED_HMAC

/**
 * \def MBEDTLS_SSL_VARIABLE_BUFFER_LENGTH
 *
 * Enable modifying the maximum I/O buffer size.
 */
#define MBEDTLS_SSL_VARIABLE_BUFFER_LENGTH // TODO: how to use? -- sounds useful for us, no?!

/* --------------------------------------------------- */

#define MBEDTLS_VERSION_FEATURES
#define MBEDTLS_VERSION_C

#define MBEDTLS_CERTS_C // test certs
#define MBEDTLS_DEBUG_C


/**
 * \def MBEDTLS_ERROR_C
 *
 * Enable error code to error string conversion.
 *
 * Module:  library/error.c
 * Caller:
 *
 * This module enables mbedtls_strerror().
 */
#define MBEDTLS_ERROR_C

#define MBEDTLS_ERROR_STRERROR_DUMMY // otherwise no string.h include which raised "implicit decl." compiler errors


#define MBEDTLS_HKDF_C
#define MBEDTLS_HMAC_DRBG_C

/**
  * Requires: MBEDTLS_PLATFORM_C
 *           MBEDTLS_PLATFORM_MEMORY (to use it within mbed TLS)
 */
//#define MBEDTLS_MEMORY_BUFFER_ALLOC_C

#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_PEM_WRITE_C
#define MBEDTLS_BASE64_C

#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PK_WRITE_C

// TODO: required?!
#define MBEDTLS_PKCS5_C
//#define MBEDTLS_PKCS11_C
#define MBEDTLS_PKCS12_C

#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_CRL_PARSE_C
#define MBEDTLS_X509_CSR_PARSE_C
#define MBEDTLS_X509_CREATE_C
#define MBEDTLS_X509_CRT_WRITE_C
#define MBEDTLS_X509_CSR_WRITE_C

//#define MBEDTLS_X509_ALLOW_EXTENSIONS_NON_V3
//#define MBEDTLS_X509_ALLOW_UNSUPPORTED_CRITICAL_EXTENSION
#define MBEDTLS_X509_CHECK_KEY_USAGE
#define MBEDTLS_X509_CHECK_EXTENDED_KEY_USAGE

//#define MBEDTLS_X509_TRUSTED_CERTIFICATE_CALLBACK

//#define MBEDTLS_X509_RSASSA_PSS_SUPPORT

/* --------------------------------------------------- */

/**
 * \def MBEDTLS_PLATFORM_C
 *
 * Enable the platform abstraction layer that allows you to re-assign
 * functions like calloc(), free(), snprintf(), printf(), fprintf(), exit().
 *
 * Enabling MBEDTLS_PLATFORM_C enables to use of MBEDTLS_PLATFORM_XXX_ALT
 * or MBEDTLS_PLATFORM_XXX_MACRO directives, allowing the functions mentioned
 * above to be specified at runtime or compile time respectively.
 *
 * \note This abstraction layer must be enabled on Windows (including MSYS2)
 * as other module rely on it for a fixed snprintf implementation.
 *
 * Module:  library/platform.c
 * Caller:  Most other .c files
 *
 * This module enables abstraction of common (libc) functions.
 */
//#define MBEDTLS_PLATFORM_C

//#define MBEDTLS_PLATFORM_MEMORY
//#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS/
//#define MBEDTLS_PLATFORM_EXIT_ALT
//#define MBEDTLS_PLATFORM_TIME_ALT
//#define MBEDTLS_PLATFORM_FPRINTF_ALT
//#define MBEDTLS_PLATFORM_PRINTF_ALT
//#define MBEDTLS_PLATFORM_SNPRINTF_ALT
//#define MBEDTLS_PLATFORM_VSNPRINTF_ALT
//#define MBEDTLS_PLATFORM_NV_SEED_ALT
//#define MBEDTLS_PLATFORM_SETUP_TEARDOWN_ALT

//#define MBEDTLS_THREADING_ALT
//#define MBEDTLS_THREADING_PTHREAD

//#define MBEDTLS_TIMING_ALT
//#define MBEDTLS_PK_RSA_ALT_SUPPORT


/* Platform options */
//#define MBEDTLS_PLATFORM_STD_MEM_HDR   <stdlib.h> /**< Header to include if MBEDTLS_PLATFORM_NO_STD_FUNCTIONS is defined. Don't define if no header is needed. */
//#define MBEDTLS_PLATFORM_STD_CALLOC        calloc /**< Default allocator to use, can be undefined */
//#define MBEDTLS_PLATFORM_STD_FREE            free /**< Default free to use, can be undefined */
//#define MBEDTLS_PLATFORM_STD_EXIT            exit /**< Default exit to use, can be undefined */
//#define MBEDTLS_PLATFORM_STD_TIME            time /**< Default time to use, can be undefined. MBEDTLS_HAVE_TIME must be enabled */
//#define MBEDTLS_PLATFORM_STD_FPRINTF      fprintf /**< Default fprintf to use, can be undefined */
//#define MBEDTLS_PLATFORM_STD_PRINTF        printf /**< Default printf to use, can be undefined */
/* Note: your snprintf must correctly zero-terminate the buffer! */
//#define MBEDTLS_PLATFORM_STD_SNPRINTF    snprintf /**< Default snprintf to use, can be undefined */
//#define MBEDTLS_PLATFORM_STD_EXIT_SUCCESS       0 /**< Default exit value to use, can be undefined */
//#define MBEDTLS_PLATFORM_STD_EXIT_FAILURE       1 /**< Default exit value to use, can be undefined */
//#define MBEDTLS_PLATFORM_STD_NV_SEED_READ   mbedtls_platform_std_nv_seed_read /**< Default nv_seed_read function to use, can be undefined */
//#define MBEDTLS_PLATFORM_STD_NV_SEED_WRITE  mbedtls_platform_std_nv_seed_write /**< Default nv_seed_write function to use, can be undefined */
//#define MBEDTLS_PLATFORM_STD_NV_SEED_FILE  "seedfile" /**< Seed file to read/write with default implementation */

/* To Use Function Macros MBEDTLS_PLATFORM_C must be enabled */
/* MBEDTLS_PLATFORM_XXX_MACRO and MBEDTLS_PLATFORM_XXX_ALT cannot both be defined */
//#define MBEDTLS_PLATFORM_CALLOC_MACRO        calloc /**< Default allocator macro to use, can be undefined */
//#define MBEDTLS_PLATFORM_FREE_MACRO            free /**< Default free macro to use, can be undefined */
//#define MBEDTLS_PLATFORM_EXIT_MACRO            exit /**< Default exit macro to use, can be undefined */
//#define MBEDTLS_PLATFORM_TIME_MACRO            time /**< Default time macro to use, can be undefined. MBEDTLS_HAVE_TIME must be enabled */
//#define MBEDTLS_PLATFORM_TIME_TYPE_MACRO       time_t /**< Default time macro to use, can be undefined. MBEDTLS_HAVE_TIME must be enabled */
//#define MBEDTLS_PLATFORM_FPRINTF_MACRO      fprintf /**< Default fprintf macro to use, can be undefined */
//#define MBEDTLS_PLATFORM_PRINTF_MACRO        printf /**< Default printf macro to use, can be undefined */
/* Note: your snprintf must correctly zero-terminate the buffer! */
//#define MBEDTLS_PLATFORM_SNPRINTF_MACRO    snprintf /**< Default snprintf macro to use, can be undefined */
//#define MBEDTLS_PLATFORM_VSNPRINTF_MACRO    vsnprintf /**< Default vsnprintf macro to use, can be undefined */
//#define MBEDTLS_PLATFORM_NV_SEED_READ_MACRO   mbedtls_platform_std_nv_seed_read /**< Default nv_seed_read function to use, can be undefined */
//#define MBEDTLS_PLATFORM_NV_SEED_WRITE_MACRO  mbedtls_platform_std_nv_seed_write /**< Default nv_seed_write function to use, can be undefined */

/* SSL options */

/** \def MBEDTLS_SSL_MAX_CONTENT_LEN
 *
 * Maximum length (in bytes) of incoming and outgoing plaintext fragments.
 *
 * This determines the size of both the incoming and outgoing TLS I/O buffers
 * in such a way that both are capable of holding the specified amount of
 * PLAINTEXT data, regardless of the protection mechanism used.
 *
 * \note On the server, there is no supported, standardized way of informing
 *       the client about restriction on the maximum size of incoming messages,
 *       and unless the limitation has been communicated by other means, it is
 *       recommended to only change the outgoing buffer size #MBEDTLS_SSL_OUT_CONTENT_LEN
 *       while keeping the default value of 16KB for the incoming buffer.
 *
 * Uncomment to set the maximum PLAINTEXT(!) size of both
 * incoming and outgoing I/O buffers.
 */
//#define MBEDTLS_SSL_MAX_CONTENT_LEN             16384
//#define MBEDTLS_SSL_IN_CONTENT_LEN              16384
//#define MBEDTLS_SSL_OUT_CONTENT_LEN             16384

//#define MBEDTLS_SSL_MAX_CONTENT_LEN             2048

#define MBEDTLS_TLS_DEFAULT_ALLOW_SHA1_IN_KEY_EXCHANGE

//#define MBEDTLS_PLATFORM_ZEROIZE_ALT
//#define MBEDTLS_PLATFORM_GMTIME_R_ALT

#include "mbedtls/check_config.h"

#endif /* MBEDTLS_CONFIG_H */
