/*
 *  mpq.c -- functions for developers using libmpq.
 *
 *  Copyright (c) 2003-2007 Maik Broemme <mbroemme@plusserver.de>
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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* libmpq main includes. */
#include "mpq.h"

/* libmpq generic includes. */
#include "common.h"

/* mpq-tools configuration includes. */
#include "config.h"

/* this function returns the library version information. */
unsigned char *libmpq__version() {

	/* return version information. */
	return (unsigned char *)VERSION;
}

/* this function reads a file and verify if it is a valid mpq archive, then it reads and decrypts the hash table. */
int libmpq__archive_open(mpq_archive *mpq_a, const char *mpq_filename) {

	/* some common variables. */
	int fd            = 0;
	unsigned int rb   = 0;
	unsigned int ncnt = FALSE;
	unsigned int i;
	struct stat fileinfo;

	/* allocate memory for the mpq archive. */
	mpq_a->mpq_l = malloc(sizeof(mpq_list));

	/* check if memory allocation was successful. */
	if (!mpq_a) {

		/* memory allocation problem. */
		return LIBMPQ_ARCHIVE_ERROR_MALLOC;
	}

	/* cleanup. */
	memset(mpq_a->mpq_l, 0, sizeof(mpq_list));

	/* allocate memory for the mpq header. */
	mpq_a->header = malloc(sizeof(mpq_header));

	/* check if memory allocation was successful. */
	if (!mpq_a->header) {

		/* memory allocation problem. */
		return LIBMPQ_ARCHIVE_ERROR_MALLOC;
	}

	/* cleanup. */
	memset(mpq_a->header, 0, sizeof(mpq_header));

	/* try to open the file. */
	fd = open(mpq_filename, O_RDONLY);

	/* check if file exists and is readable */
	if (fd == -1) {

		/* file could not be opened. */
		return LIBMPQ_ARCHIVE_ERROR_OPEN;
	}

	/* fill the structures with informations */
	strncpy((char *)mpq_a->filename, mpq_filename, strlen(mpq_filename));
	libmpq__decrypt_buffer_init(mpq_a);
	mpq_a->fd = fd;
	mpq_a->header->id = 0;
	mpq_a->maxblockindex = 0;

	/* loop through file and search for mpq signature. */
	while (!ncnt) {

		/* reset header values. */
		mpq_a->header->id = 0;

		/* seek in file. */
		lseek(mpq_a->fd, mpq_a->mpqpos, SEEK_SET);

		/* read header from file. */
		rb = read(mpq_a->fd, mpq_a->header, sizeof(mpq_header));

		/* if different number of bytes read, break the loop. */
		if (rb != sizeof(mpq_header)) {

			/* no valid mpq archive. */
			return LIBMPQ_ARCHIVE_ERROR_FORMAT;
		}

		/* special offset for protected mpq archives. */
		if (mpq_a->header->offset == LIBMPQ_MPQ_HEADER_W3M) {

			/* mpq archive is protected, so set header. */
			mpq_a->flags |= LIBMPQ_MPQ_FLAG_PROTECTED;
			mpq_a->header->offset = sizeof(mpq_header);
		}

		/* if valid signature has been found, break the loop. */
		if (mpq_a->header->id == LIBMPQ_MPQ_HEADER_ID &&
		    mpq_a->header->offset == sizeof(mpq_header) &&
		    mpq_a->header->hashtablepos < mpq_a->header->archivesize &&
		    mpq_a->header->blocktablepos < mpq_a->header->archivesize) {

			/* break the loop, because header was found. */
			ncnt = TRUE;
		}

		/* check if we already found a valid mpq header. */
		if (!ncnt) {

			/* move to the next possible offset. */
			mpq_a->mpqpos += 0x200;
		}
	}

	/* get the right positions of the hash table and the block table. */
	mpq_a->blocksize = (0x200 << mpq_a->header->blocksize);
	fstat(mpq_a->fd, &fileinfo);

	/* normal mpq archives must have position of 0x200. */
	if (mpq_a->header->hashtablepos + mpq_a->mpqpos < fileinfo.st_size &&
	    mpq_a->header->blocktablepos + mpq_a->mpqpos < fileinfo.st_size) {

		/* set the right position into header. */
		mpq_a->header->hashtablepos  += mpq_a->mpqpos;
		mpq_a->header->blocktablepos += mpq_a->mpqpos;
	} else {

		/* no right hashtable and blocktable found. */
		return LIBMPQ_ARCHIVE_ERROR_FORMAT;
	}

	/* try to read and decrypt the hashtable. */
	if (libmpq__read_table_hash(mpq_a) != 0) {

		/* the hashtable seems corrupt. */
		return LIBMPQ_ARCHIVE_ERROR_HASHTABLE;
	}

	/* try to read and decrypt the blocktable. */
	if (libmpq__read_table_block(mpq_a) != 0) {

		/* the blocktable seems corrupt. */
		return LIBMPQ_ARCHIVE_ERROR_BLOCKTABLE;
	}

	/* TODO: include the cool filelist from last file in mpq archive here. */
	/* allocate memory for the file list. */
	mpq_a->mpq_l->mpq_files = malloc(mpq_a->header->blocktablesize * sizeof(char *));

	/* check if memory allocation was successful. */
	if (!mpq_a->mpq_l->mpq_files) {

		/* memory allocation problem. */
		return LIBMPQ_ARCHIVE_ERROR_MALLOC;
	}

	/* loop through all files in mpq archive. */
	for (i = 0; i < mpq_a->header->blocktablesize; i++) {

		/* allocate memory for the filelist element. */
		mpq_a->mpq_l->mpq_files[i] = malloc(sizeof(char *));

		/* check if memory allocation was successful. */
		if (!mpq_a->mpq_l->mpq_files[i]) {

			/* memory allocation problem. */
			return LIBMPQ_ARCHIVE_ERROR_MALLOC;
		}

		/* create the filename. */
		snprintf((char *)mpq_a->mpq_l->mpq_files[i], PATH_MAX, "file%06i.xxx", i + 1);
	}

	/* if no error was found, return zero. */
	return LIBMPQ_SUCCESS;
}

/* this function closes the file descriptor, frees the decryption buffer and the filelist. */
int libmpq__archive_close(mpq_archive *mpq_a) {

	/* some common variables. */
	unsigned int i;

	/* free the filelist. */
	for (i = 0; i < mpq_a->header->blocktablesize; i++) {

		/* free the filelist element. */
		free(mpq_a->mpq_l->mpq_files[i]);
	}

	/* free the filelist pointer. */
	free(mpq_a->mpq_l->mpq_files);

	/* cleanup. */
	memset(mpq_a->buf, 0, sizeof(mpq_a->buf));

	/* free the allocated memory. */
	free(mpq_a->header);
	free(mpq_a->mpq_l);

	/* check if file descriptor is valid. */
	if ((close(mpq_a->fd)) == -1) {

		/* file was not opened. */
		return LIBMPQ_ARCHIVE_ERROR_CLOSE;
	}

	/* if no error was found, return zero. */
	return LIBMPQ_SUCCESS;
}

/* this function returns some information for the requested type of a mpq archive. */
int libmpq__archive_info(mpq_archive *mpq_a, unsigned int infotype) {

	/* some common variables. */
	unsigned int filecount = 0;
	unsigned int fsize     = 0;
	unsigned int csize     = 0;
	mpq_block *mpq_b_end   = mpq_a->blocktable + mpq_a->header->blocktablesize;
	mpq_block *mpq_b       = NULL;

	/* check which information type should be returned. */
	switch (infotype) {
		case LIBMPQ_ARCHIVE_SIZE:

			/* return the archive size. */
			return mpq_a->header->archivesize;
		case LIBMPQ_ARCHIVE_HASHTABLE_SIZE:

			/* return the hashtable size. */
			return mpq_a->header->hashtablesize;
		case LIBMPQ_ARCHIVE_BLOCKTABLE_SIZE:

			/* return the blocktable size. */
			return mpq_a->header->blocktablesize;
		case LIBMPQ_ARCHIVE_BLOCKSIZE:

			/* return the blocksize. */
			return mpq_a->blocksize;
		case LIBMPQ_ARCHIVE_NUMFILES:

			/* loop through all files in archive and count them. */
			for (mpq_b = mpq_a->blocktable; mpq_b < mpq_b_end; mpq_b++) {
				filecount++;
			}

			/* return the number of files in the mpq archive. */
			return filecount;
		case LIBMPQ_ARCHIVE_COMPRESSED_SIZE:

			/* loop through all files in archive and count compressed size. */
			for (mpq_b = mpq_a->blocktable; mpq_b < mpq_b_end; mpq_b++) {
				csize += mpq_b->csize;
			}

			/* return the compressed size of all files in the mpq archive. */
			return csize;
		case LIBMPQ_ARCHIVE_UNCOMPRESSED_SIZE:

			/* loop through all files in archive and count uncompressed size. */
			for (mpq_b = mpq_a->blocktable; mpq_b < mpq_b_end; mpq_b++) {
				fsize += mpq_b->fsize;
			}

			/* return the uncompressed size of all files in the mpq archive. */
			return fsize;
		default:

			/* if no error was found, return zero. */
			return LIBMPQ_SUCCESS;
	}

	/* if no error was found, return zero. */
	return LIBMPQ_SUCCESS;
}

/* this function returns some useful file information. */
int libmpq__file_info(mpq_archive *mpq_a, unsigned int infotype, const unsigned int number) {

	/* some common variables. */
	mpq_block *mpq_b = NULL;
	mpq_hash *mpq_h  = NULL;
	int blockindex   = -1;
	unsigned int i;

	/* check if given number is not out of range. */
	if (number < 1 || number > mpq_a->header->blocktablesize) {

		/* file number is out of range. */
		return LIBMPQ_FILE_ERROR_RANGE;
	}

	/* search for correct hashtable. */
	for (i = 0; i < mpq_a->header->hashtablesize; i++) {
		if ((number - 1) == (mpq_a->hashtable[i]).blockindex) {

			/* correct hashtable found. */
			blockindex = (mpq_a->hashtable[i]).blockindex;
			mpq_h = &(mpq_a->hashtable[i]);

			/* break execution. */
			break;
		}
	}

	/* check if file was found. */
	if (blockindex == -1 || blockindex > mpq_a->header->blocktablesize) {

		/* file was not found in mpq archive. :( */
		return LIBMPQ_FILE_ERROR_EXIST;
	}

	/* check if sizes are correct. */
	mpq_b = mpq_a->blocktable + blockindex;
	if (mpq_b->filepos > (mpq_a->header->archivesize + mpq_a->mpqpos) || mpq_b->csize > mpq_a->header->archivesize) {

		/* file is corrupt in mpq archive. */
		return LIBMPQ_FILE_ERROR_CORRUPT;
	}

	/* check if file exists. */
	if ((mpq_b->flags & LIBMPQ_FILE_EXISTS) == 0) {

		/* file does not exist in mpq archive. */
		return LIBMPQ_FILE_ERROR_EXIST;
	}

	/* check which information type should be returned. */
	switch (infotype) {
		case LIBMPQ_FILE_COMPRESSED_SIZE:

			/* return the compressed size of the file in the mpq archive. */
			return mpq_b->csize;
		case LIBMPQ_FILE_UNCOMPRESSED_SIZE:

			/* return the uncompressed size of the file in the mpq archive. */
			return mpq_b->fsize;
		case LIBMPQ_FILE_COMPRESSION_TYPE:

			/* check if compression type is pkware. */
			if (mpq_b->flags & LIBMPQ_FILE_COMPRESS_PKWARE) {

				/* return the compression type pkware. */
				return LIBMPQ_FILE_COMPRESS_PKWARE;
			}

			/* check if compression type is multi. */
			if (mpq_b->flags & LIBMPQ_FILE_COMPRESS_MULTI) {

				/* return the compression type multi. */
				return LIBMPQ_FILE_COMPRESS_MULTI;
			}
		default:

			/* if no error was found, return zero. */
			return LIBMPQ_SUCCESS;
	}

	/* if no error was found, return zero. */
	return LIBMPQ_SUCCESS;
}

/* this function returns filename by the given number. */
unsigned char *libmpq__file_name(mpq_archive *mpq_a, const unsigned int number) {

	/* check if we are in the range of available files. */
	if (number < 1 || number > mpq_a->header->blocktablesize) {

		/* file not found by number, so return NULL. */
		return NULL;
	}

	/* return the filename. */
	return mpq_a->mpq_l->mpq_files[number - 1];
}

/* this function returns filenumber by the given name. */
int libmpq__file_number(mpq_archive *mpq_a, const unsigned char *name) {

	/* some common variables. */
	unsigned int i;

	/* loop through all filenames in mpq archive. */
	for (i = 0; mpq_a->mpq_l->mpq_files[i]; i++) {

		/* check if given filename was found in list. */
		if (strncmp((const char *)mpq_a->mpq_l->mpq_files[i], (const char *)name, strlen((const char *)name)) == 0) {

			/* if file found return the number */
			return i + 1;
		}
	}

	/* if no matching entry found, so return error. */
	return LIBMPQ_FILE_ERROR_EXIST;
}

/* this function extracts a file from a mpq archive by the given number. */
int libmpq__file_extract(mpq_archive *mpq_a, const unsigned int number) {

	/* some common variables. */
	mpq_file *mpq_f  = NULL;
	mpq_block *mpq_b = NULL;
	mpq_hash *mpq_h  = NULL;
	int blockindex   = -1;
	int fd           = 0;
	int transferred  = 1;
	int i;
	unsigned char buffer[0x1000];

	/* check if we are in the range of available files. */
	if (number < 1 || number > mpq_a->header->blocktablesize) {

		/* file not found by number, so return with error. */
		return LIBMPQ_FILE_ERROR_RANGE;
	}

	/* open file in write mode. */
	fd = open((const char *)mpq_a->mpq_l->mpq_files[number - 1], O_RDWR|O_CREAT|O_TRUNC, 0644);

	/* check if file could be written. */
	if (fd == -1) {

		/* file could not be created, so return with error. */
		return LIBMPQ_FILE_ERROR_OPEN;
	}

	/* search for correct hashtable. */
	for (i = 0; i < mpq_a->header->hashtablesize; i++) {
		if ((number - 1) == (mpq_a->hashtable[i]).blockindex) {

			/* correct hashtable found. */
			blockindex = (mpq_a->hashtable[i]).blockindex;
			mpq_h = &(mpq_a->hashtable[i]);

			/* break execution. */
			break;
		}
	}

	/* check if file was found. */
	if (blockindex == -1 || blockindex > mpq_a->header->blocktablesize) {

		/* file was not found in mpq archive. :( */
		return LIBMPQ_FILE_ERROR_EXIST;
	}

	/* check if sizes are correct. */
	mpq_b = mpq_a->blocktable + blockindex;
	if (mpq_b->filepos > (mpq_a->header->archivesize + mpq_a->mpqpos) || mpq_b->csize > mpq_a->header->archivesize) {

		/* file is corrupt in mpq archive. */
		return LIBMPQ_FILE_ERROR_CORRUPT;
	}

	/* check if file exists. */
	if ((mpq_b->flags & LIBMPQ_FILE_EXISTS) == 0) {

		/* file does not exist in mpq archive. */
		return LIBMPQ_FILE_ERROR_EXIST;
	}

	/* allocate memory for file structure */
	mpq_f = malloc(sizeof(mpq_file));
	if (!mpq_f) {

		/* memory allocation problem. */
		return LIBMPQ_FILE_ERROR_MALLOC;
	}

	/* initialize file structure. */
	memset(mpq_f, 0, sizeof(mpq_file));
	mpq_f->fd = fd;
	mpq_f->mpq_b = mpq_b;
	mpq_f->nblocks = (mpq_f->mpq_b->fsize + mpq_a->blocksize - 1) / mpq_a->blocksize;
	mpq_f->mpq_h = mpq_h;
	mpq_f->accessed = FALSE;
	mpq_f->blockposloaded = FALSE;
	snprintf((char *)mpq_f->filename, PATH_MAX, (const char *)mpq_a->mpq_l->mpq_files[number - 1]);

	/* allocate buffers for decompression. */
	if (mpq_f->mpq_b->flags & LIBMPQ_FILE_COMPRESSED) {

		/* allocate buffer for block positions. */
		if ((mpq_f->blockpos = malloc(sizeof(int) * mpq_f->nblocks + 1)) == NULL) {

			/* memory allocation problem. */
			return LIBMPQ_FILE_ERROR_MALLOC;
		}
	}

	/* loop until whole file content is written. */
	while (transferred > 0) {

		/* read file until its end. */
		transferred = libmpq__read_file_mpq(mpq_a, mpq_f, mpq_f->filepos, buffer, sizeof(buffer));

		/* check if we reached end of file. */
		if (transferred == 0) {

			/* break execution. */
			break;
		} else {

			/* file was already processed in this loop. */
			mpq_f->accessed  = TRUE;
			mpq_f->filepos  += transferred;
		}

		/* write file to disk. */
		transferred = write(mpq_f->fd, buffer, transferred);

		/* check if write operations was successful. */
		if (transferred == 0) {

			/* break execution. */
			break;
		}
	}

	/* check if file descriptor is valid. */
	if ((close(fd)) == -1) {

		/* file was not opened. */
		return LIBMPQ_FILE_ERROR_CLOSE;
	}

	/* freeing the file structure. */
	free(mpq_f);

	/* if no error was found, return zero. */
	return LIBMPQ_SUCCESS;
}
