/*
 *  extract.c -- global extracting function for all known file compressions
 *               in a mpq archive.
 *
 *  Copyright (c) 2003-2008 Maik Broemme <mbroemme@plusserver.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* generic includes. */
#include <stdlib.h>
#include <string.h>

/* zlib includes. */
#include <zlib.h>

/* libmpq main includes. */
#include "mpq.h"

/* libmpq generic includes. */
#include "explode.h"
#include "extract.h"
#include "huffman.h"
#include "wave.h"

/* table with decompression bits and functions. */
static decompress_table_s dcmp_table[] = {
	{0x01, libmpq__decompress_huffman},	/* decompression using huffman trees. */
	{0x02, libmpq__decompress_zlib},	/* decompression with the zlib library. */
	{0x08, libmpq__decompress_pkzip},	/* decompression with pkware data compression library. */
	{0x10, libmpq__decompress_bzip2},	/* decompression with bzip2 library. */
	{0x40, libmpq__decompress_wave_mono},	/* decompression for mono waves. */
	{0x80, libmpq__decompress_wave_stereo}	/* decompression for stereo waves. */
};

/* this function decompress a stream using huffman algorithm. */
int libmpq__decompress_huffman(unsigned char *out_buf, int out_size, unsigned char *in_buf, int in_size) {

	/* TODO: make typdefs of this structs? */
	/* some common variables. */
	int result = 0;
	int tb     = 0;
	struct huffman_tree_s *ht;
	struct huffman_tree_item_s *hi;
	struct huffman_input_stream_s *is;

	/* allocate memory for the huffman tree. */
	if ((ht = malloc(sizeof(struct huffman_tree_s))) == NULL ||
	    (hi = malloc(sizeof(struct huffman_tree_item_s))) == NULL ||
	    (is = malloc(sizeof(struct huffman_input_stream_s))) == NULL) {

		/* memory allocation problem. */
		return LIBMPQ_ARCHIVE_ERROR_MALLOC;
	}

	/* cleanup structures. */
	memset(ht, 0, sizeof(struct huffman_tree_s));
	memset(hi, 0, sizeof(struct huffman_tree_item_s));
	memset(is, 0, sizeof(struct huffman_input_stream_s));

	/* initialize input stream. */
	is->bit_buf  = *(unsigned int *)in_buf;
	in_buf      += sizeof(int);
	is->in_buf   = (unsigned char *)in_buf;
	is->bits     = 32;

// TODO: add all the mallocs to init function and add function libmpq__huffman_tree_free() */
//	if ((result = libmpq__huffman_tree_init(ht, hi, LIBMPQ_HUFF_DECOMPRESS)) < 0) {
//
//		/* something on zlib initialization failed. */
//		return LIBMPQ_FILE_ERROR_DECOMPRESS;
//	}

	/* initialize the huffman tree for decompression. */
	libmpq__huffman_tree_init(ht, hi, LIBMPQ_HUFF_DECOMPRESS);

	/* save the number of copied bytes. */
	tb = libmpq__do_decompress_huffman(ht, is, out_buf, out_size);

	/* free input stream if used. */
	if (is != NULL) {

		/* free input stream. */
		free(is);
	}

	/* free huffman item if used. */
	if (hi != NULL) {

		/* free huffman item stream. */
		free(hi);
	}

	/* free huffman tree if used. */
	if (ht != NULL) {

		/* free huffman tree stream. */
		free(ht);
	}

	/* if no error was found, return zero. */
	return tb;
}

/* this function decompress a stream using zlib algorithm. */
int libmpq__decompress_zlib(unsigned char *out_buf, int out_size, unsigned char *in_buf, int in_size) {

	/* some common variables. */
	int result = 0;
	int tb     = 0;
	z_stream z;

	/* fill the stream structure for zlib. */
	z.next_in   = (Bytef *)in_buf;
	z.avail_in  = (uInt)in_size;
	z.total_in  = in_size;
	z.next_out  = (Bytef *)out_buf;
	z.avail_out = (uInt)out_size;
	z.total_out = 0;
	z.zalloc    = NULL;
	z.zfree     = NULL;

	/* initialize the decompression structure, storm.dll uses zlib version 1.1.3. */
	if ((result = inflateInit(&z)) != Z_OK) {

		/* something on zlib initialization failed. */
		return LIBMPQ_FILE_ERROR_DECOMPRESS;
	}

	/* call zlib to decompress the data. */
	if ((result = inflate(&z, Z_FINISH)) != Z_STREAM_END) {

		/* something on zlib decompression failed. */
		return LIBMPQ_FILE_ERROR_DECOMPRESS;
	}

	/* save transferred bytes. */
	tb = z.total_out;

	/* cleanup zlib. */
	if ((result = inflateEnd(&z)) != Z_OK) {

		/* something on zlib finalization failed. */
		return LIBMPQ_FILE_ERROR_DECOMPRESS;
	}

	/* return transferred bytes. */
	return tb;
}

/* this function decompress a stream using pkzip algorithm. */
int libmpq__decompress_pkzip(unsigned char *out_buf, int out_size, unsigned char *in_buf, int in_size) {

	/* some common variables. */
	int tb = 0;
	unsigned char *work_buf;
	pkzip_data_s info;

	/* allocate memory for pkzip data structure. */
	if ((work_buf = malloc(sizeof(pkzip_cmp_s))) == NULL) {

		/* memory allocation problem. */
		return LIBMPQ_ARCHIVE_ERROR_MALLOC;
	}

	/* cleanup. */
	memset(work_buf, 0, sizeof(pkzip_cmp_s));

	/* fill data information structure. */
	info.in_buf   = in_buf;
	info.in_pos   = 0;
	info.in_bytes = in_size;
	info.out_buf  = out_buf;
	info.out_pos  = 0;
	info.max_out  = out_size;

	/* do the decompression. */
	if ((tb = libmpq__do_decompress_pkzip(work_buf, &info)) < 0) {

		/* free working buffer if used. */
		if (work_buf != NULL) {

			/* free working buffer. */
			free(work_buf);
		}

		/* something failed on pkzip decompression. */
		return tb;
	}

	/* save transferred bytes. */
	tb = info.out_pos;

	/* free working buffer if used. */
	if (work_buf != NULL) {

		/* free working buffer. */
		free(work_buf);
	}

	/* if no error was found, return zero. */
	return tb;
}

/* this function decompress a stream using bzip2 library. */
int libmpq__decompress_bzip2(unsigned char *out_buf, int out_size, unsigned char *in_buf, int in_size) {

	/* TODO: add bzip2 decompression here. */
	/* if no error was found, return zero. */
	return LIBMPQ_FILE_ERROR_DECOMPRESS;
}

/* this function decompress a stream using wave algorithm. (1 channel) */
int libmpq__decompress_wave_mono(unsigned char *out_buf, int out_size, unsigned char *in_buf, int in_size) {

	/* some common variables. */
	int tb = 0;

	/* save the number of copied bytes. */
	if ((tb = libmpq__do_decompress_wave(out_buf, out_size, in_buf, in_size, 1)) < 0) {

		/* something on wave decompression failed. */
		return tb;
	}

	/* return transferred bytes. */
	return tb;
}

/* this function decompress a stream using wave algorithm. (2 channels) */
int libmpq__decompress_wave_stereo(unsigned char *out_buf, int out_size, unsigned char *in_buf, int in_size) {

	/* some common variables. */
	int tb = 0;

	/* save the number of copied bytes. */
	if ((tb = libmpq__do_decompress_wave(out_buf, out_size, in_buf, in_size, 2)) < 0) {

		/* something on wave decompression failed. */
		return tb;
	}

	/* return transferred bytes. */
	return tb;
}

/* this function decompress a stream using a combination of the other compression algorithm. */
int libmpq__decompress_multi(unsigned char *out_buf, int out_size, unsigned char *in_buf, int in_size) {

	/* some common variables. */
	int tb                  = 0;
	unsigned int count      = 0;
	unsigned int entries    = (sizeof(dcmp_table) / sizeof(decompress_table_s));
	unsigned char *temp_buf = NULL;
	unsigned char *work_buf;
	unsigned char decompress_flag;
	unsigned int i;

	/* check if the input size is the same as output size, so do nothing. */
	if (in_size == out_size) {

		/* check if buffer have same data. */
		if (in_buf == out_buf) {

			/* return output buffer size. */
			return out_size;
		}

		/* copy buffer to target. */
		memcpy(out_buf, in_buf, in_size);

		/* return output buffer size. */
		return out_size;
	}

	/* get applied compression types. */
	decompress_flag = *in_buf++;

	/* decrement data size. */
	in_size--;

	/* search decompression table type and get all types of compression. */
	for (i = 0; i < entries; i++) {

		/* check if have to apply this decompression. */
		if (decompress_flag & dcmp_table[i].mask) {

			/* increase counter for used compression algorithms. */
			count++;
		}
	}

	/* check if there is some method unhandled. (e.g. compressed by future versions) */
	if (count == 0) {

		/* compression type is unknown and we need to implement it. :) */
		return LIBMPQ_FILE_ERROR_DECOMPRESS;
	}

	/* if multiple decompressions should be made, we need temporary buffer for the data. */
	if (count > 1) {

		/* allocate memory for temporary buffer. */
		if ((temp_buf = malloc(out_size)) == NULL) {

			/* memory allocation problem. */
			return LIBMPQ_ARCHIVE_ERROR_MALLOC;
		}

		/* cleanup. */
		memset(temp_buf, 0, out_size);
	}

	/* apply all decompressions. */
	for (i = 0, count = 0; i < entries; i++) {

		/* check if not used this kind of compression. */
		if (decompress_flag & dcmp_table[i].mask) {

			/* if multiple decompressions should be made, we need temporary buffer for the data. */
			if (count == 0) {

				/* use output buffer as working buffer. */
				work_buf = out_buf;
			} else {

				/* use temporary buffer as working buffer. */
				work_buf = temp_buf;
			}

			/* decompress buffer using corresponding function. */
			if ((tb = dcmp_table[i].decompress(work_buf, out_size, in_buf, in_size)) < 0) {

				/* free temporary buffer if used. */
				if (temp_buf != NULL) {

					/* free temporary buffer. */
					free(temp_buf);
				}

				/* something on decompression failed. */
				return tb;
			}

			/* move output size to source size for next compression. */
			in_size  = out_size;
			in_buf     = work_buf;

			/* increase counter. */
			count++;
		}
	}

	/* if output buffer is not the same like target buffer, we have to copy data (this will happen on multiple decompressions). */
	if (work_buf != out_buf) {

		/* copy buffer. */
		memcpy(out_buf, in_buf, out_size);
	}

	/* free temporary buffer if used. */
	if (temp_buf != NULL) {

		/* free temporary buffer. */
		free(temp_buf);
	}

	/* return transferred bytes.. */
	return tb;
}
