/* miniz.c 2.0.8 - public domain deflate/inflate, zlib-subset, ZIP
   reading/writing/appending, PNG writing See "unlicense" statement at the end
   of this file. Rich Geldreich <richgel99@gmail.com>, last updated Oct. 13,
   2013 Implements RFC 1950: http://www.ietf.org/rfc/rfc1950.txt and RFC 1951:
   http://www.ietf.org/rfc/rfc1951.txt


   Minimal implementation of inflate.
*/
#ifndef POLYMER_INFLATE_H_
#define POLYMER_INFLATE_H_

#include <cstdint>

namespace polymer {

typedef unsigned char mz_uint8;
typedef signed short mz_int16;
typedef unsigned short mz_uint16;
typedef unsigned int mz_uint32;
typedef unsigned int mz_uint;
typedef int64_t mz_int64;
typedef uint64_t mz_uint64;
typedef int mz_bool;
typedef unsigned long mz_ulong;

enum {
  MZ_OK = 0,
  MZ_STREAM_END = 1,
  MZ_NEED_DICT = 2,
  MZ_ERRNO = -1,
  MZ_STREAM_ERROR = -2,
  MZ_DATA_ERROR = -3,
  MZ_MEM_ERROR = -4,
  MZ_BUF_ERROR = -5,
  MZ_VERSION_ERROR = -6,
  MZ_PARAM_ERROR = -10000
};

/* Single-call decompression. */
/* Returns MZ_OK on success, or one of the error codes from mz_inflate() on
 * failure. */
int mz_uncompress(unsigned char* pDest, mz_ulong* pDest_len, const unsigned char* pSource, mz_ulong source_len);

} // namespace polymer

#endif
