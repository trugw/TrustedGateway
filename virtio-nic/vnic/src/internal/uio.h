/*
 * Copyright (c) 2013, Google, Inc. All rights reserved
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __LIB_TRUSTY_UIO_H
#define __LIB_TRUSTY_UIO_H

#include <sys/types.h>

typedef struct iovec_kern {
	void		*base;
	size_t		len;
} iovec_kern_t;


ssize_t membuf_to_kern_iovec(const iovec_kern_t *iov, unsigned int iov_cnt,
                             const uint8_t *buf, size_t len);

ssize_t membuf_to_kern_iovec_skip_begin(const iovec_kern_t *iov, unsigned int iov_cnt,
                             const uint8_t *buf, size_t len,
                             size_t prefix_skip);

ssize_t kern_iovec_to_membuf(uint8_t *buf, size_t len,
                             const iovec_kern_t *iov, unsigned int iov_cnt);

ssize_t kern_iovec_to_membuf_skip_begin(uint8_t *buf, size_t len,
                             const iovec_kern_t *iov, unsigned int iov_cnt,
                             size_t prefix_skip);

#endif
