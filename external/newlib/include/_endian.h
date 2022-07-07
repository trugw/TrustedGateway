#ifndef __NEWLIB_MACHINE_ENDIAN_H__
#error "must be included via <newlib/endian.h>"
#endif /* !__NEWLIB_MACHINE_ENDIAN_H__ */

#ifndef _LITTLE_ENDIAN
#define	_LITTLE_ENDIAN	1234
#endif

#ifndef _BIG_ENDIAN
#define	_BIG_ENDIAN	4321
#endif

#ifndef _PDP_ENDIAN
#define	_PDP_ENDIAN	3412
#endif

#ifndef _BYTE_ORDER
#if defined(__IEEE_BIG_ENDIAN) || defined(__IEEE_BYTES_BIG_ENDIAN)
#define	_BYTE_ORDER	_BIG_ENDIAN
#else
#define	_BYTE_ORDER	_LITTLE_ENDIAN
#endif

#endif
