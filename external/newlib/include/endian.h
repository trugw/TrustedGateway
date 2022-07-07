#ifndef __NEWLIB_MACHINE_ENDIAN_H__
#define	__NEWLIB_MACHINE_ENDIAN_H__

#include <newlib/_endian.h>

#if _BYTE_ORDER == _LITTLE_ENDIAN
#define	_QUAD_HIGHWORD	1
#define	_QUAD_LOWWORD	0
#else
#define	_QUAD_HIGHWORD	0
#define	_QUAD_LOWWORD	1
#endif

#define	LITTLE_ENDIAN	_LITTLE_ENDIAN
#define	BIG_ENDIAN	_BIG_ENDIAN
#define	PDP_ENDIAN	_PDP_ENDIAN
#define	BYTE_ORDER	_BYTE_ORDER

#ifdef __GNUC__
#define	__bswap16(_x)	__builtin_bswap16(_x)
#define	__bswap32(_x)	__builtin_bswap32(_x)
#define	__bswap64(_x)	__builtin_bswap64(_x)
#else /* __GNUC__ */
static __inline __uint16_t
__bswap16(__uint16_t _x)
{

	return ((__uint16_t)((_x >> 8) | ((_x << 8) & 0xff00)));
}

static __inline __uint32_t
__bswap32(__uint32_t _x)
{

	return ((__uint32_t)((_x >> 24) | ((_x >> 8) & 0xff00) |
	    ((_x << 8) & 0xff0000) | ((_x << 24) & 0xff000000)));
}

static __inline __uint64_t
__bswap64(__uint64_t _x)
{

	return ((__uint64_t)((_x >> 56) | ((_x >> 40) & 0xff00) |
	    ((_x >> 24) & 0xff0000) | ((_x >> 8) & 0xff000000) |
	    ((_x << 8) & ((__uint64_t)0xff << 32)) |
	    ((_x << 24) & ((__uint64_t)0xff << 40)) |
	    ((_x << 40) & ((__uint64_t)0xff << 48)) | ((_x << 56))));
}
#endif /* !__GNUC__ */

#if _BYTE_ORDER == _LITTLE_ENDIAN
#define	htonl(_x)	__bswap32(_x)
#define	htons(_x)	__bswap16(_x)
#define	ntohl(_x)	__bswap32(_x)
#define ntohs(_x)	__bswap16(_x)
#else
#define	htonl(_x)	((__uint32_t)(_x))
#define	htons(_x)	((__uint16_t)(_x))
#define	ntohl(_x)	((__uint32_t)(_x))
#define ntohs(_x)	((__uint16_t)(_x))
#endif

#if _BYTE_ORDER == _LITTLE_ENDIAN
#define htobe16(x) __bswap16 (x)
#define htole16(x) (x)
#define be16toh(x) __bswap16 (x)
#define le16toh(x) (x)

#define htobe32(x) __bswap32 (x)
#define htole32(x) (x)
#define be32toh(x) __bswap32 (x)
#define le32toh(x) (x)

#define htobe64(x) __bswap64 (x)
#define htole64(x) (x)
#define be64toh(x) __bswap64 (x)
#define le64toh(x) (x)

#else
#define htobe16(x) (x)
#define htole16(x) __bswap16 (x)
#define be16toh(x) (x)
#define le16toh(x) __bswap16 (x)

#define htobe32(x) (x)
#define htole32(x) __bswap32 (x)
#define be32toh(x) (x)
#define le32toh(x) __bswap32 (x)

#define htobe64(x) (x)
#define htole64(x) __bswap64 (x)
#define be64toh(x) (x)
#define le64toh(x) __bswap64 (x)
#endif

#endif /* __NEWLIB_MACHINE_ENDIAN_H__ */
