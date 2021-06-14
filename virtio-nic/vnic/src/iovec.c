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

#include <internal/err.h>
#include <internal/uio.h>

#include <string.h>
#include <assert.h>

#ifndef unlikely
#define unlikely(x)      __builtin_expect(!!(x), 0)
#endif

#ifndef likely
#define likely(x)      __builtin_expect(!!(x), 1)
#endif

ssize_t membuf_to_kern_iovec(const iovec_kern_t *iov, unsigned int iov_cnt,
                             const uint8_t *buf, size_t len)
{
	size_t copied = 0;

	if (unlikely(iov_cnt == 0 || len == 0))
		return 0;

	if (unlikely(iov == NULL || buf == NULL))
		return (ssize_t) ERR_INVALID_ARGS;

	for (unsigned int i = 0; i < iov_cnt; i++, iov++) {

		size_t to_copy = len;
		if (to_copy > iov->len)
			to_copy = iov->len;

		if (unlikely(to_copy == 0))
			continue;

		if (unlikely(iov->base == NULL))
			return (ssize_t) ERR_INVALID_ARGS;

		memcpy(iov->base, buf, to_copy);

		copied += to_copy;
		buf    += to_copy;
		len    -= to_copy;

		if (len == 0)
			break;
	}

	return  (ssize_t) copied;
}

ssize_t membuf_to_kern_iovec_skip_begin(const iovec_kern_t *iov, unsigned int iov_cnt,
                             const uint8_t *buf, size_t len,
                             size_t prefix_skip)
{
	if (unlikely(prefix_skip == 0)) return membuf_to_kern_iovec(iov, iov_cnt, buf, len);

	if (unlikely(iov_cnt == 0 || len == 0))
		return 0;

	if (unlikely(buf == NULL || iov == NULL))
		return (ssize_t) ERR_INVALID_ARGS;

	size_t copied = 0;
	size_t skipped = 0;
	unsigned int i = 0;

	for (i = 0; i < iov_cnt; i++, iov++) {
		size_t next_len = iov->len;
		size_t rest_buf = 0;

		if ( (skipped + next_len) < prefix_skip ) {
			skipped += next_len;
			continue;
		}

		// bytes of this iovec buffer which need to be included in the copy (get overwritten)
		rest_buf = (skipped + next_len) - prefix_skip;

		if (rest_buf > 0) { 
			size_t to_copy = rest_buf;
			if (to_copy > len)
				to_copy = len;

			if (unlikely(iov->base == NULL))
				return (ssize_t) ERR_INVALID_ARGS;

			memcpy((uint8_t *)iov->base + (next_len - rest_buf), buf, rest_buf);

			// already enough data, no need to consider next iovs
			if (to_copy == len) return len;
		}

		// 2nd copy step will start from next iovec and tries to fill the rest
		skipped += (next_len - rest_buf);
		assert(skipped == prefix_skip);
		
		copied = rest_buf;

		iov++;
		iov_cnt -= (i + 1);
		buf += copied; // shift offset
		len -= copied; // decrease rest-copy length
		break;
	}

	if (skipped < prefix_skip) return ERR_INVALID_ARGS;

	return copied + membuf_to_kern_iovec(iov, iov_cnt, buf, len);
}

ssize_t kern_iovec_to_membuf(uint8_t *buf, size_t len,
                             const iovec_kern_t *iov, unsigned int iov_cnt)
{
	size_t copied = 0;

	if (unlikely(iov_cnt == 0 || len == 0))
		return 0;

	if (unlikely(buf == NULL || iov == NULL))
		return (ssize_t) ERR_INVALID_ARGS;

	for (unsigned int i = 0; i < iov_cnt; i++, iov++) {

		size_t to_copy = len;
		if (to_copy > iov->len)
			to_copy = iov->len;

		if (unlikely(to_copy == 0))
			continue;

		if (unlikely(iov->base == NULL))
			return (ssize_t) ERR_INVALID_ARGS;

		memcpy (buf, iov->base, to_copy);

		copied += to_copy;
		buf    += to_copy;
		len    -= to_copy;

		if (len == 0)
			break;
	}

	return (ssize_t) copied;
}

ssize_t kern_iovec_to_membuf_skip_begin(uint8_t *buf, size_t len,
                             const iovec_kern_t *iov, unsigned int iov_cnt,
                             size_t prefix_skip)
{
	if (unlikely(prefix_skip == 0)) return kern_iovec_to_membuf(buf, len, iov, iov_cnt);

	if (unlikely(iov_cnt == 0 || len == 0))
		return 0;

	if (unlikely(buf == NULL || iov == NULL))
		return (ssize_t) ERR_INVALID_ARGS;

	size_t copied = 0;
	size_t skipped = 0;
	unsigned int i = 0;

	for (i = 0; i < iov_cnt; i++, iov++) {
		size_t next_len = iov->len;
		size_t rest_buf = 0;

		if ( (skipped + next_len) < prefix_skip ) {
			skipped += next_len;
			continue;
		}

		// bytes of this iovec buffer which need to be included in the copy
		rest_buf = (skipped + next_len) - prefix_skip;

		if (rest_buf > 0) { 
			size_t to_copy = rest_buf;
			if (to_copy > len)
				to_copy = len;

			if (unlikely(iov->base == NULL))
				return (ssize_t) ERR_INVALID_ARGS;

			memcpy(buf, (uint8_t *)iov->base + (next_len - rest_buf), rest_buf);

			// already enough data, no need to consider next iovs
			if (to_copy == len) return len;
		}

		// 2nd copy step will start from next iovec and tries to fill the rest
		skipped += (next_len - rest_buf);
		assert(skipped == prefix_skip);
		
		copied = rest_buf;

		iov++;
		iov_cnt -= (i + 1);
		buf += copied; // shift offset
		len -= copied; // decrease rest-copy length
		break;
	}

	if (skipped < prefix_skip) return ERR_INVALID_ARGS;

	return copied + kern_iovec_to_membuf(buf, len, iov, iov_cnt);
}
