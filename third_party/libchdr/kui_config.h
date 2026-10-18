/* K-UI build configuration; upstream sources remain unchanged.
 * Used ONLY for third_party/libchdr translation units. */
#ifndef KUI_LIBCHDR_CONFIG_H
#define KUI_LIBCHDR_CONFIG_H
#undef MINIZ_NO_MALLOC
#undef MINIZ_NO_ZLIB_APIS
#ifndef MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ARCHIVE_APIS
#endif
#ifndef MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_DEFLATE_APIS
#endif
#ifndef MINIZ_NO_STDIO
#define MINIZ_NO_STDIO
#endif
#ifndef MINIZ_NO_TIME
#define MINIZ_NO_TIME
#endif
#ifndef MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#endif
#define WANT_RAW_DATA_SECTOR 1
#define WANT_SUBCODE 1
#define VERIFY_BLOCK_CRC 1
#define LOWRAM_TARGET 1
#define CHDR_CD_SCRATCH_BUFFER 0
/* Capture uses an older miniz with its original public symbol names.
 * Keep this decoder's independently pinned miniz completely separate. */
#define miniz_def_alloc_func kui_chdr_miniz_def_alloc_func
#define miniz_def_free_func kui_chdr_miniz_def_free_func
#define miniz_def_realloc_func kui_chdr_miniz_def_realloc_func
#define mz_adler32 kui_chdr_mz_adler32
#define mz_crc32 kui_chdr_mz_crc32
#define mz_error kui_chdr_mz_error
#define mz_free kui_chdr_mz_free
#define mz_inflate kui_chdr_mz_inflate
#define mz_inflateEnd kui_chdr_mz_inflateEnd
#define mz_inflateInit kui_chdr_mz_inflateInit
#define mz_inflateInit2 kui_chdr_mz_inflateInit2
#define mz_inflateReset kui_chdr_mz_inflateReset
#define mz_uncompress kui_chdr_mz_uncompress
#define mz_uncompress2 kui_chdr_mz_uncompress2
#define mz_version kui_chdr_mz_version
#define tinfl_decompress kui_chdr_tinfl_decompress
#define tinfl_decompress_mem_to_callback kui_chdr_tinfl_decompress_mem_to_callback
#define tinfl_decompress_mem_to_heap kui_chdr_tinfl_decompress_mem_to_heap
#define tinfl_decompress_mem_to_mem kui_chdr_tinfl_decompress_mem_to_mem
#define tinfl_decompressor_alloc kui_chdr_tinfl_decompressor_alloc
#define tinfl_decompressor_free kui_chdr_tinfl_decompressor_free
#endif
