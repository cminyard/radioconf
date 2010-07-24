/*
 *  yaesu_rw - Interface to Yaesu radio clone interfaces
 *  Copyright (C) 2009  Corey Minyard
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *    Boston, MA  02110-1301  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <malloc.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/select.h>
#include <stdarg.h>
#include <ctype.h>

#ifndef YAESU_CONFIGDIR
#define YAESU_CONFIGDIR "/etc/yaesuconf"
#endif

char *version = PACKAGE_VERSION;

static struct option long_options[] = {
    {"help",	 0, NULL, '?'},
    {"hash",	 0, NULL, 'h'},
    {"verbose",  0, NULL, 'v'},
    {"device",	 1, NULL, 'd'},
    {"speed",	 1, NULL, 's'},
    {"ignerr",	 0, NULL, 'i'},
    {"notermio", 0, NULL, 'n'},
    {"read",	 0, NULL, 'r'},
    {"write",	 0, NULL, 'w'},
    {"rcv_echo", 0, NULL, 'e'},
    {"norcv_echo",0, NULL, 'm'},
    {"send_echo",0, NULL, 'y'},
    {"checksum", 0, NULL, 'c'},
    {"nochecksum",0, NULL, 'g'},
    {"waitchecksum", 0, NULL, 'j'},
    {"nowaitchecksum", 0, NULL, 'k'},
    {"checkblock", 0, NULL, 'p'},
    {"nocheckblock",0, NULL, 'q'},
    {"configdir",0, NULL, 'f'},
    {NULL,	 0, NULL, 0}
};
char *progname = NULL;
int verbose = 0;
int hash = 0;

#define YAESU_TIMEOUT(d, s, u) do { d->timeout.tv_sec = s; d->timeout.tv_usec = u; } while(0)
#define YAESU_TEST_TIMEOUT(d) YAESU_TIMEOUT(d, 1, 0)
#define YAESU_CHAR_TIMEOUT(d) YAESU_TIMEOUT(d, 5, 0)
#define YAESU_TIMING_TIMEOUT(d) YAESU_TIMEOUT(d, 0, 200000)
#define YAESU_WAITWRITE_TIMEOUT(d) YAESU_TIMEOUT(d, 0, 5000)
#define YAESU_WAITCHUNK_TIMEOUT(d) YAESU_TIMEOUT(d, 0, d->waitchunk)
#define YAESU_START_TIMEOUT(d) YAESU_TIMEOUT(d, 10, 0)
#define YAESU_MAX_RETRIES 5

enum yaesu_read_state {
    YAESU_STATE_WAITFIRSTBLOCK,
    YAESU_STATE_INFIRSTBLOCK,
    YAESU_STATE_WAITBLOCK,
    YAESU_STATE_INBLOCK,
    YAESU_STATE_WAITCSUM,
    YAESU_STATE_CSUM,
    YAESU_STATE_CSUM_WAITACK,
    YAESU_STATE_DONE
};

#define BUFF_ALLOC_INC 16

#define MAX_BLOCK_SIZE 256
struct yaesu_block {
    unsigned int len;
    unsigned int alloc_len;
    unsigned char *buff;
    struct yaesu_block *next, *prev;
};

struct yaesu_blocksizes {
    /* Note: Last one of nblocks in the array must be 0, it will repeat. */
    unsigned short nblocks;
    unsigned short block_len;
};

struct yaesu_data {
    int is_read;
    enum yaesu_read_state state;
    int rfd;
    int wfd;
    struct timeval timeout;
    unsigned int pos;
    unsigned int rpos;
    unsigned int retries;
    struct yaesu_block head;
    struct yaesu_block *curr_block;
    unsigned int data_count;
    unsigned int block_count;
    int send_echo;
    int recv_echo;
    unsigned int expect_len;
    int timeout_mode;
    unsigned int csum;
    unsigned int block_len;
    struct yaesu_blocksizes *bsizes;
    unsigned int bsize_left;
    unsigned int curr_bsize;
    int check_write;
    unsigned char write_buf[65536];
    unsigned int write_start;
    unsigned int write_len;
    unsigned int waitchunk;
    unsigned int chunksize;
    int waiting_chunk_done;
    int check_write2;
    int has_checksum;
    int has_checkblock;
    int waitchecksum;
};


const unsigned char testbuf[8] = { '0', '1', '2', '3', '4', '5', '6', '7' };
const unsigned char ack[1] = { 0x06 };

struct yaesu_data *
alloc_yaesu_data(int rfd, int wfd, int is_read,
		 int send_echo, int recv_echo, int has_checksum,
		 int has_checkblock, int waitchecksum)
{
    struct yaesu_data *d;

    d = malloc(sizeof(*d));
    if (!d)
	return NULL;
    d->is_read = is_read;
    d->state = YAESU_STATE_WAITFIRSTBLOCK;
    if (is_read)
	YAESU_START_TIMEOUT(d);
    else
	YAESU_CHAR_TIMEOUT(d);
    d->retries = 0;
    d->pos = 0;
    d->data_count = 0;
    d->block_count = 0;
    d->head.next = &d->head;
    d->head.prev = &d->head;
    d->csum = 0;
    d->rfd = rfd;
    d->wfd = wfd;
    d->check_write = 0;
    d->check_write2 = 0;
    d->write_start = 0;
    d->write_len = 0;
    d->chunksize = 32;
    d->waitchunk = 100000;
    d->waiting_chunk_done = 0;
    d->send_echo = send_echo;
    d->recv_echo = recv_echo;
    d->bsizes = NULL;
    d->timeout_mode = 0;
    d->has_checksum = has_checksum;
    d->has_checkblock = has_checkblock;
    d->waitchecksum = waitchecksum;

    return d;
}

unsigned int
max_blocksize(struct yaesu_data *d)
{
    struct yaesu_blocksizes *b = d->bsizes;
    unsigned int max = 0;

    if (!b)
	return d->block_len;

    for (;;) {
	if (b->block_len > max)
	    max = b->block_len;
	if (!b->nblocks)
	    break;
	b++;
    }
    return max;
}

void
free_yaesu_data(struct yaesu_data *d)
{
    struct yaesu_block *b, *bf;

    b = d->head.next;
    while (b != &d->head) {
	bf = b;
	b = b->next;
	if (bf->buff)
	    free(bf->buff);
	free(bf);
    }
    free(d);
}

void
yaesu_get_timeout(struct yaesu_data *d, struct timeval *tv)
{
    *tv = d->timeout;
}

int
yaesu_check_write(struct yaesu_data *d)
{
    return d->check_write || d->check_write2;
}

int
yaesu_is_done(struct yaesu_data *d)
{
    return d->state == YAESU_STATE_DONE;
}

void
yaesu_next_block_len(struct yaesu_data *d)
{
   if (d->bsizes) {
	if (d->bsize_left == 0) {
	    d->curr_bsize++;
	    d->bsize_left = d->bsizes[d->curr_bsize].nblocks;
	    d->block_len = d->bsizes[d->curr_bsize].block_len;
	    if (d->bsize_left == 0)
		d->bsizes = NULL;
	}
	d->bsize_left--;
    }
}

struct yaesu_block *
yaesu_new_block(struct yaesu_data *d)
{
    struct yaesu_block *b;

    yaesu_next_block_len(d);

    b = malloc(sizeof(*b));
    if (!b)
	return NULL;
    b->buff = malloc(BUFF_ALLOC_INC);
    if (!b->buff) {
	free(b);
	return NULL;
    }
    d->block_count++;
    b->len = 0;
    b->alloc_len = BUFF_ALLOC_INC;
    b->next = &d->head;
    b->prev = d->head.prev;
    b->next->prev = b;
    b->prev->next = b;
    return b;
}

struct yaesu_conf {
    char *name;
    unsigned char *header;
    unsigned int header_cmp_len;
    unsigned int header_len;
    unsigned int data_len;
    unsigned int block_size;
    int echo;
    int has_checksum;
    int has_checkblock;
    int waitchecksum;
    struct yaesu_blocksizes *bsizes;
    struct yaesu_conf *next;
};

struct yaesu_conf *radios = NULL;

unsigned int
max_header_size(void)
{
    struct yaesu_conf *r;
    unsigned int max = 0;

    for(r = radios; r; r = r->next) {
	if (r->header_len > max)
	    max = r->header_len;
    }
    return max;
}

int
check_yaesu_type(struct yaesu_data *d, unsigned char *buff, unsigned int len,
		 int do_default)
{
    struct yaesu_conf *r;

    for(r = radios; r; r = r->next) {
	if (len != r->header_len)
	    continue;
	if (memcmp(r->header, buff, r->header_cmp_len) == 0) {
	    /* found it */
	    printf("Found a %s\n", r->name);
	    d->expect_len = r->data_len;
	    if (r->bsizes) {
		d->bsizes = r->bsizes;
		d->curr_bsize = 0;
		d->bsize_left = r->bsizes[0].nblocks;
		d->block_len = r->bsizes[0].block_len;
	    } else
		d->block_len = r->block_size;
	    if (d->recv_echo < 0)
		d->recv_echo = r->echo;
	    if (d->has_checksum < 0)
		d->has_checksum = r->has_checksum;
	    if (d->has_checkblock < 0)
		d->has_checkblock = r->has_checkblock;
	    if (d->waitchecksum < 0)
		d->waitchecksum = r->waitchecksum;
	    return 1;
	}
    }

    if (do_default) {
	printf("Unable to find the device, going ahead, but it probably won't"
	       " work\n");
	d->expect_len = 1000000;
	d->block_len = 64;
	if (d->recv_echo < 0)
	    d->recv_echo = 0;
	if (d->has_checksum < 0)
	    d->has_checksum = 0;
	if (d->has_checkblock < 0)
	    d->has_checkblock = 0;
	if (d->waitchecksum < 0)
	    d->waitchecksum = 1;
	d->timeout_mode = 1;
	return 1;
    }

    return 0;
}

int
append_yaesu_data(struct yaesu_data *d, struct yaesu_block *b,
		  unsigned char *buf, unsigned int len)
{
    unsigned int new_len = b->len + len;

    if (new_len > b->alloc_len) {
	unsigned int alloc_len;
	unsigned char *nb;

	alloc_len = b->alloc_len + BUFF_ALLOC_INC;
	while (alloc_len < new_len)
	    alloc_len = b->alloc_len + BUFF_ALLOC_INC;
	nb = malloc(alloc_len);
	if (!nb)
	    return ENOMEM;
	memcpy(nb, b->buff, b->len);
	free(b->buff);
	b->alloc_len = alloc_len;
	b->buff = nb;
    }
    memcpy(b->buff + b->len, buf, len);
    b->len += len;
    return 0;
}

int
add_yaesu_block(struct yaesu_data *d, unsigned char *data, unsigned int size)
{
    struct yaesu_block *b;

    b = yaesu_new_block(d);
    if (!b)
	return ENOMEM;
    b->buff = malloc(size);
    if (!b->buff) {
	free(b);
	return ENOMEM;
    }
    memcpy(b->buff, data, size);
    b->len = size;
    d->data_count += size;
    return 0;
}

int
yaesu_write(struct yaesu_data *d, const unsigned char *data, unsigned int len)
{
    unsigned int end;
    int rv;
    unsigned int total_written = 0;

    if (len + d->write_len > sizeof(d->write_buf))
	return ENOBUFS;

    if (len > 0) {
	if (verbose > 1) {
	    unsigned int i;
	    printf("Write:");
	    for (i = 0; i < len; i++)
		printf(" %2.2x", data[i]);
	    printf("\n");
	    fflush(stdout);
	}

	end = (d->write_start + d->write_len) % sizeof(d->write_buf);
	if ((end + len) < sizeof(d->write_buf))
	    memcpy(d->write_buf + end, data, len);
	else {
	    unsigned int left = 256 - end;
	    memcpy(d->write_buf + end, data, left);
	    memcpy(d->write_buf, data + left, len - left);
	}
	d->write_len += len;
    }

    end = (d->write_start + d->write_len) % sizeof(d->write_buf);
    if (d->write_len > 0) {
	int to_write;

	if (end > d->write_start) {
	write_nowrap:
	    if (total_written + d->write_len > d->chunksize)
		to_write = d->chunksize - total_written;
	    else
		to_write = d->write_len;
	    rv = write(d->wfd, d->write_buf + d->write_start, to_write);
	    if (rv < 0) {
		if (errno == EAGAIN)
		    goto not_all_written;
		return errno;
	    }
	    total_written += rv;
	    if (rv > 0 && verbose > 2) {
		int i;
		printf("Write(2):");
		for (i = 0; i < rv; i++)
		    printf(" %2.2x", d->write_buf[d->write_start+i]);
		printf("\n");
		fflush(stdout);
	    }

	    d->write_len -= rv;
	    d->write_start += rv;
	    if (total_written >= d->chunksize)
		goto out_delay;
	    if (d->write_len > 0)
		goto not_all_written;
	} else {
	    unsigned int wrc;
	    /*
	     * Note that this also covers a completely full buffer, where
	     * end == write_start.
	     */
	    wrc = sizeof(d->write_buf) - d->write_start;
	    if (total_written + wrc > d->chunksize)
		to_write = d->chunksize - total_written;
	    else
		to_write = d->write_len;
	    rv = write(d->wfd, d->write_buf + d->write_start, to_write);
	    if (rv < 0) {
		if (errno == EAGAIN)
		    goto not_all_written;
		return errno;
	    }
	    total_written += rv;
	    if (rv > 0 && verbose > 2) {
		int i;
		printf("Write(2):");
		for (i = 0; i < rv; i++)
		    printf(" %2.2x", d->write_buf[d->write_start+i]);
		printf("\n");
		fflush(stdout);
	    }
	    d->write_len -= rv;
	    d->write_start += rv;
	    if (d->write_start >= sizeof(d->write_buf)) {
		/* We wrote it all, move to the beginning of the buffer. */
		d->write_start = 0;
		if (d->write_len > 0)
		    /* Finish up the write. */
		    goto write_nowrap;
	    } else if (total_written >= d->chunksize)
		goto out_delay;
	    else
		goto not_all_written;
	}
    }

    d->check_write2 = 0;
    return 0;

 out_delay:
    d->check_write2 = 0;
    YAESU_WAITWRITE_TIMEOUT(d);
    d->waiting_chunk_done = 1;
    return 0;

 not_all_written:
    d->check_write2 = 1;
    return 0;
}

int
yaesu_check_block(struct yaesu_data *d, struct yaesu_block *b)
{
    unsigned int i;
    unsigned int sum;

    if (!d->has_checkblock)
	return 0;
    if (b->len < 2)
	return EINVAL;
    if (b->buff[0] != (d->block_count - 1))
	return EBADMSG;
    sum = 0;
    for (i = 1; i < b->len - 1; i++) {
	sum += b->buff[i];
	b->buff[i-1] = b->buff[i];
    }
    b->len -= 2; /* We remove the block number and the checksum */
    if (b->buff[b->len + 1] != (sum & 0xff))
	return EBADMSG;
    return 0;
}

int
handle_yaesu_read_data(struct yaesu_data *d)
{
    unsigned char rbuf[16];
    unsigned char *buf = rbuf;
    int rv, err;
    unsigned int i, len;
    struct yaesu_block *b;

    rv = read(d->rfd, buf, sizeof(buf));
    if (rv < 0) {
	if (errno == EAGAIN)
	    return 0;
	return errno;
    } else if (rv == 0)
	return 0;
    len = rv;

    if (verbose > 1) {
	printf("Read:");
	for (i = 0; i < len; i++)
	    printf(" %2.2x", buf[i]);
	printf("\n");
	fflush(stdout);
    }

    if (d->send_echo) {
	/* Radio echos everything received. */
	int err = yaesu_write(d, buf, len);
	if (err)
	    return err;
    }

    switch(d->state) {
    case YAESU_STATE_WAITFIRSTBLOCK:
	d->state = YAESU_STATE_INFIRSTBLOCK;
	b = yaesu_new_block(d);
	if (!b)
	    return ENOMEM;
	YAESU_CHAR_TIMEOUT(d);
	/* FALLTHROUGH */

    case YAESU_STATE_INFIRSTBLOCK:
	b = d->head.prev;

	if ((len + b->len) > MAX_BLOCK_SIZE)
	    return EINVAL;
	err = append_yaesu_data(d, b, buf, len);
	if (err)
	    return err;
	d->data_count += len;
	for (i = 0; i < len; i++)
	    d->csum += buf[i];

	if (check_yaesu_type(d, b->buff, b->len, 0)) {
	    if (hash) {
		printf(".");
		fflush(stdout);
	    }
	    d->state = YAESU_STATE_WAITBLOCK;
	    rv = yaesu_write(d, ack, 1);
	    if (rv)
		return rv;
	}
	break;

    case YAESU_STATE_WAITBLOCK:
	if (d->recv_echo) {
	    if (buf[0] != ack[0])
		return EINVAL;
	    buf++;
	    len--;
	}
	d->state = YAESU_STATE_INBLOCK;
	b = yaesu_new_block(d);
	if (!b)
	    return ENOMEM;
	if (len == 0)
	    break;
	/* FALLTHROUGH */

    case YAESU_STATE_INBLOCK:
	b = d->head.prev;

	if (!d->timeout_mode && ((len + b->len) > d->block_len))
	    return EINVAL;
	err = append_yaesu_data(d, b, buf, len);
	if (err)
	    return err;
	d->data_count += len;
	for (i = 0; i < len; i++)
	    d->csum += buf[i];

	if (d->data_count > d->expect_len)
	    return EINVAL;
	else if (d->data_count == d->expect_len) {
	    rv = yaesu_check_block(d, b);
	    if (rv)
		return rv;
	    if (d->has_checksum)
		d->state = YAESU_STATE_WAITCSUM;
	    else
		d->state = YAESU_STATE_DONE;
	    rv = yaesu_write(d, ack, 1);
	    if (rv)
		return rv;
	} else if (!d->timeout_mode && (b->len == d->block_len)) {
	    if (hash) {
		printf(".");
		fflush(stdout);
	    }
	    rv = yaesu_check_block(d, b);
	    if (rv)
		return rv;
	    d->state = YAESU_STATE_WAITBLOCK;
	    rv = yaesu_write(d, ack, 1);
	    if (rv)
		return rv;
	}
	break;

    case YAESU_STATE_WAITCSUM:
	if (d->recv_echo) {
	    if (buf[0] != ack[0])
		return EINVAL;
	    buf++;
	    len--;
	}
	d->state = YAESU_STATE_CSUM;
	if (len == 0)
	    break;
	/* FALLTHROUGH */

    case YAESU_STATE_CSUM:
	if ((d->csum & 0xff) != buf[0])
	    /* Checksum failure. */
	    return EBADMSG;
	d->state = YAESU_STATE_DONE;
	rv = yaesu_write(d, ack, 1);
	if (rv)
	    return rv;
	break;

    case YAESU_STATE_CSUM_WAITACK:
    case YAESU_STATE_DONE:
	break;
    }

    return 0;
}

int
handle_yaesu_read_timeout(struct yaesu_data *d)
{
    int rv;
    unsigned int i;
    struct yaesu_block *b;

    switch(d->state) {
    case YAESU_STATE_INFIRSTBLOCK:
	goto timeout2;

    case YAESU_STATE_INBLOCK:
    case YAESU_STATE_WAITBLOCK:
	if (d->timeout_mode)
	    goto timeout2;
	/* FALLTHRU */
    case YAESU_STATE_WAITFIRSTBLOCK:
    case YAESU_STATE_WAITCSUM:
    case YAESU_STATE_CSUM:
	return ETIMEDOUT;

    case YAESU_STATE_CSUM_WAITACK:
    case YAESU_STATE_DONE:
	break;
    }

    return 0;

 timeout2:
    switch(d->state) {
    case YAESU_STATE_INFIRSTBLOCK:
	b = d->head.prev;
	check_yaesu_type(d, b->buff, b->len, 1);
	YAESU_TIMING_TIMEOUT(d);
	printf("Going into timer mode, dumping block information\n");
	printf("Header size = %d\n", b->len);
	printf("Header data is:");
	for (i = 0; i < b->len; i++)
	    printf(" %2.2x", b->buff[i]);
	printf("\nBlock sizes:");
	fflush(stdout);
	
	if (hash) {
	    printf(".");
	    fflush(stdout);
	}
	d->state = YAESU_STATE_WAITBLOCK;
	rv = yaesu_write(d, ack, 1);
	if (rv)
	    return rv;
	break;
	
    case YAESU_STATE_INBLOCK:
	b = d->head.prev;
	printf(" %d", b->len);
	fflush(stdout);
	if (hash) {
	    printf(".");
	    fflush(stdout);
	}
	d->state = YAESU_STATE_WAITBLOCK;
	rv = yaesu_write(d, ack, 1);
	if (rv)
	    return rv;
	break;

    case YAESU_STATE_WAITBLOCK:
	/* Must be end of data.  The last block will be the checksum. */
	b = d->head.prev;
	if (b->len == 0) {
	    printf("Last data block was 0 bytes, not a checksum?\n");
	    return EBADMSG;
	}
	b->len--;
	d->csum -= b->buff[b->len];
	if (d->has_checksum && (d->csum & 0xff) != b->buff[b->len])
	    /* Checksum failure. */
	    return EBADMSG;
	d->state = YAESU_STATE_DONE;
	break;

    default:
	abort();
    }
    return 0;
}

int
yaesu_start_write(struct yaesu_data *d)
{
    struct yaesu_block *b;
    int rv;
    unsigned int i;
    unsigned int to_write;

    d->data_count = 0;
    d->curr_block = d->head.next;
    d->rpos = 0;
    if (hash) {
	printf(".");
	fflush(stdout);
    }
    b = d->curr_block;
    for (i = 0; i < b->len; i++)
	d->csum += b->buff[i];

    to_write = b->len - d->pos;
    rv = yaesu_write(d, b->buff + d->pos, to_write);
    if (rv == ENOBUFS)
	d->check_write = 1;
    else if (rv)
	return rv;
    else {
	d->pos = to_write;
	d->data_count += to_write;
    }
    return 0;
}

int
handle_yaesu_write_ready(struct yaesu_data *d)
{
    struct yaesu_block *b;
    int rv;
    unsigned int to_write;

    if (d->is_read)
	return yaesu_write(d, NULL, 0);

    if (d->state == YAESU_STATE_WAITCSUM) {
	unsigned char cs[1];
	cs[0] = d->csum;
	rv = yaesu_write(d, cs, 1);
	if (rv == ENOBUFS)
	    return 0;
	else if (rv)
	    return rv;
	if (!d->recv_echo)
	    d->state = YAESU_STATE_CSUM_WAITACK;
	else
	    d->state = YAESU_STATE_CSUM;
	d->check_write = 0;
	return 0;
    }

    b = d->curr_block;
    to_write = b->len - d->pos;
    rv = yaesu_write(d, b->buff + d->pos, to_write);
    if (rv == ENOBUFS)
	return 0;
    else if (rv)
	return rv;
    d->check_write = 0;
    d->pos += to_write;
    d->data_count += to_write;
    return 0;
}

int
handle_yaesu_write_data(struct yaesu_data *d)
{
    struct yaesu_block *b;
    unsigned char rbuf[16];
    unsigned char *buf = rbuf;
    int rv;
    unsigned int i, len;

    rv = read(d->rfd, buf, sizeof(buf));
    if (rv < 0) {
	if (errno == EAGAIN)
	    return 0;
	return errno;
    } else if (rv == 0)
	return 0;
    len = rv;

    if (verbose > 1) {
	printf("Read (%d):", d->state);
	for (i = 0; i < len; i++)
	    printf(" %2.2x", buf[i]);
	printf("\n");
	fflush(stdout);
    }

    if (d->send_echo) {
	int err = yaesu_write(d, buf, len);
	if (err)
	    return err;
    }

    if (d->state == YAESU_STATE_DONE)
	return 0;

    if (d->state == YAESU_STATE_CSUM_WAITACK) {
	rv = yaesu_write(d, ack, 1);
	if (rv)
	    return rv;
	d->state = YAESU_STATE_DONE;
	return 0;
    }

    if (d->state == YAESU_STATE_CSUM) {
	if (buf[0] != (d->csum & 0xff))
	    return EINVAL;
	d->state = YAESU_STATE_CSUM_WAITACK;
	return 0;
    }

    b = d->curr_block;
    if (d->recv_echo && (d->rpos < b->len)) {
	b = d->curr_block;
	for (i = 0; (i < len) && (d->rpos < b->len); i++, d->rpos++) {
	    if (buf[i] != b->buff[d->rpos])
		return EINVAL;
	}
	if (d->rpos < b->len)
	    /* More echos to receive, so just return. */
	    return 0;

	len -= i;
	buf += i;
	/* Might have the ack, so go on. */
    }

    b = b->next;
    if (d->has_checksum && !d->waitchecksum && (b == &d->head))
	/* Ugly, but if we don't wait for the ack before sending the
	   checksum, then send it now. */
	goto send_checksum;

    if (len == 0)
	return 0;

    if (d->check_write)
	/* We haven't written all the data, so we shouldn't read an ack. */
	return EINVAL;

    /* We should have the ack. */
    if (len != 1)
	return EINVAL;
    if (buf[0] != ack[0])
	return EINVAL;
    /* Note that in radio mode, the ack echo was already sent */

    if (hash) {
	printf(".");
	fflush(stdout);
    }

    if (b == &d->head) {
	/* We just sent the last block */
	if (d->has_checksum)
	    goto send_checksum;
	else
	    d->state = YAESU_STATE_DONE;
    } else {
	/* Prepare the next block */
	d->curr_block = b;
	d->rpos = 0;
	d->pos = 0;
	for (i = 0; i < b->len; i++)
	    d->csum += b->buff[i];
	return handle_yaesu_write_ready(d);
    }

    return 0;

 send_checksum:
    {
	unsigned char cs[1];
	cs[0] = d->csum;
	rv = yaesu_write(d, cs, 1);
	if (rv == ENOBUFS) {
	    d->state = YAESU_STATE_WAITCSUM;
	    d->check_write = 1;
	} else if (rv)
	    return rv;
	if (!d->recv_echo)
	    d->state = YAESU_STATE_CSUM_WAITACK;
	else
	    d->state = YAESU_STATE_CSUM;
    }
    return 0;
}

int
handle_yaesu_write_timeout(struct yaesu_data *d)
{
    int rv;

    if (d->waiting_chunk_done == 1) {
	int left;
	rv = ioctl(d->wfd, TIOCOUTQ, &left);
	if (rv < 0)
	    return errno;
	if (left == 0) {
	    YAESU_WAITCHUNK_TIMEOUT(d);
	    d->waiting_chunk_done = 2;
	}
	return 0;
    } else if (d->waiting_chunk_done) {
	YAESU_CHAR_TIMEOUT(d);
	d->waiting_chunk_done = 0;
	d->check_write2 = 1;
	return 0;
    }
    return ETIMEDOUT;
}

int
handle_yaesu_data(struct yaesu_data *d)
{
    if (d->is_read)
	return handle_yaesu_read_data(d);
    else
	return handle_yaesu_write_data(d);
}

int
handle_yaesu_timeout(struct yaesu_data *d)
{
    if (d->is_read)
	return handle_yaesu_read_timeout(d);
    else
	return handle_yaesu_write_timeout(d);
}

int
get_one_line(FILE *f, char **oline, unsigned int *linenum)
{
    char *line = malloc(80);
    int alloc_len = 80;
    int len;

    if (!line)
	return -ENOMEM;

 restart:
    if (fgets(line, alloc_len, f) == NULL) {
	free(line);
	return 0; /* End of file */
    }

    len = strlen(line);
    if (len == 0)
	goto restart;

    /* Look for partial line reads and \\ at the end of the line. */
    for (;;) {
	int extend = 0;
	char *nl, *nl2;
	ssize_t len2;

	if (line[len-1] == '\n') {
	    (*linenum)++;
	    len--;
	    line[len] = '\0';
	    if (line[len-1] == '\\') {
		len--;
		line[len] = '\0';
		extend = 1;
	    }
	} else
	    extend = 1;

	if (!extend)
	    break;

	nl = malloc(80);
	if (!nl) {
	    free(line);
	    return -ENOMEM;
	}

	if (fgets(nl, 80, f) == NULL) {
	    /* End of file. */
	    free(nl);
	    break;
	}

	len2 = strlen(nl);
	if (len2 == 0) {
	    free(nl);
	    break;
	}

	nl2 = malloc(len + len2 + 1);
	if (!nl2) {
	    free(nl);
	    free(line);
	    return -ENOMEM;
	}
	strcpy(nl2, line);
	strcat(nl2, nl);
	free(line);
	free(nl);
	len += len2;
	line = nl2;
    }

    /* Remove trailing spaces */
    while (len > 0 && isspace(line[len-1])) {
	line[len] = '\0';
	len--;
    }

    if (len == 0)
	goto restart;

    *oline = line;
    return len;
}

void
conferr(unsigned int linenum, char *str, ...)
{
    va_list ap;

    va_start(ap, str);
    fprintf(stderr, "Error on line %d: ", linenum);
    vfprintf(stderr, str, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

unsigned long
strtoul_nooctal(char *str, char **ep)
{
    while(*str == '0')
	str++;
    return strtoul(str, ep, 0);
}

int
read_yaesu_config(char *configdir)
{
    FILE *f;
    int in_radio = 0;
    unsigned int linenum = 0;;
    struct yaesu_conf *r = NULL;
    char *ep;
    static const char *fname = "radios";
    char *configfile = malloc(strlen(configdir) + strlen(fname) + 2);

    if (!configfile)
      return ENOMEM;

    strcpy(configfile, configdir);
    strcat(configfile, "/");
    strcat(configfile, fname);

    f = fopen(configfile, "r");
    if (!f) {
	perror(configfile);
	exit(1);
    }

    for (;;) {
	char *line, *tok, *nexttok;
	int len;

	len = get_one_line(f, &line, &linenum);
	if (len < 0)
	    return -len;
	if (len == 0)
	    break;
	nexttok = NULL;
	tok = strtok_r(line, " \t", &nexttok);
	if (!tok)
	    goto line_done;

	if (line[0] == '#')
	    goto line_done;

	if (!in_radio) {
	    if (strcmp(tok, "radio") != 0)
		conferr(linenum, "Expected 'radio' to start defining a radio's"
			" config\n");

	    tok = strtok_r(NULL, " \t", &nexttok);
	    if (!tok)
		conferr(linenum, "Expected name");

	    in_radio = 1;
	    r = malloc(sizeof(*r));
	    if (!r)
		conferr(linenum, "out of memory\n");
	    memset(r, 0, sizeof(*r));
	    r->echo = -1;
	    r->has_checksum = -1;
	    r->has_checkblock = -1;
	    r->waitchecksum = -1;
	    r->name = strdup(tok);
	    if (!r->name)
		conferr(linenum, "out of memory\n");
	    goto line_done;
	}

	if (strcmp(tok, "endradio") == 0) {
	    in_radio = 0;
	    if (!r->header_cmp_len)
		conferr(linenum, "radio has no header\n");
	    if (!r->data_len)
		conferr(linenum, "radio has no file length\n");
	    if (!r->block_size && !r->bsizes)
		conferr(linenum, "radio has no block size\n");

	    if (!r->header_len)
		r->header_len = r->header_cmp_len;
	    if (r->echo == -1)
		r->echo = 0;
	    if (r->has_checksum == -1)
		r->has_checksum = 0;
	    if (r->has_checkblock == -1)
		r->has_checkblock = 0;
	    if (r->waitchecksum == -1)
		r->waitchecksum = 1;

	    r->next = radios;
	    radios = r;
	    goto line_done;
	}

	if (strcmp(tok, "headercmp") == 0) {
	    unsigned int alloc_len = 16;
	    
	    if (r->header)
		conferr(linenum, "Header already specified");
	    r->header = malloc(alloc_len);
	    if (!r->header)
		conferr(linenum, "out of memory\n");
	    while ((tok = strtok_r(NULL, " \t", &nexttok))) {
		unsigned long v = strtoul(tok, &ep, 16);

		if (*ep || (v > 0xff))
		    conferr(linenum, "Invalid hex char");
		if (r->header_cmp_len >= alloc_len) {
		    unsigned char *nh;

		    alloc_len += 16;
		    nh = malloc(alloc_len);
		    if (!nh)
			return ENOMEM;
		    memcpy(nh, r->header, r->header_cmp_len);
		    free(r->header);
		    r->header = nh;
		}
		r->header[r->header_cmp_len] = v;
		r->header_cmp_len++;
	    }

	    if (r->header_cmp_len == 0)
		conferr(linenum, "Header compare cannot be empty");

	    if (r->header_len && (r->header_len < r->header_cmp_len))
		conferr(linenum, "Header length cannot less than header"
			" compare");
	    goto line_done;
	}

	if (strcmp(tok, "headerlen") == 0) {
	    if (r->header_len)
		conferr(linenum, "Header length already specified");

	    tok = strtok_r(NULL, " \t", &nexttok);
	    if (!tok)
		conferr(linenum, "Expected number");

	    r->header_len = strtoul_nooctal(tok, &ep);
	    if (*ep)
		conferr(linenum, "Invalid header length");

	    if (r->header_len == 0)
		conferr(linenum, "Header length cannot be zero");
	    if (r->header_cmp_len && (r->header_len < r->header_cmp_len))
		conferr(linenum, "Header length cannot less than header"
			" compare");
	    goto line_done;
	}

	if (strcmp(tok, "filesize") == 0) {
	    if (r->data_len)
		conferr(linenum, "File size already specified");

	    tok = strtok_r(NULL, " \t", &nexttok);
	    if (!tok)
		conferr(linenum, "Expected number");

	    r->data_len = strtoul_nooctal(tok, &ep);
	    if (*ep)
		conferr(linenum, "Invalid file size");

	    if (r->data_len == 0)
		conferr(linenum, "file size length cannot be zero");

	    goto line_done;
	}

	if (strcmp(tok, "blocksize") == 0) {
	    if (r->block_size)
		conferr(linenum, "Block size already specified");

	    tok = strtok_r(NULL, " \t", &nexttok);
	    if (!tok)
		conferr(linenum, "Expected number");

	    r->block_size = strtoul_nooctal(tok, &ep);
	    if (*ep)
		conferr(linenum, "Invalid block size");

	    if (r->block_size == 0)
		conferr(linenum, "block size length cannot be zero");

	    goto line_done;
	}

	if (strcmp(tok, "blocksizelist") == 0) {
	    unsigned int i;
	    enum ts { LPAREN, FIRST, COMMA, SECOND, RPAREN } st = LPAREN;
	    struct yaesu_blocksizes *b;
	    unsigned int blen = 0;
	    unsigned int alloc_len = 16;
	    unsigned int cnt = 0, sz = 0;

	    if (r->bsizes)
		conferr(linenum, "blocksizelist already specified");
	    if (r->block_size)
		conferr(linenum, "blocksize already specified");

	    b = malloc(sizeof(*b) * 16);
	    for (tok = strtok_r(NULL, "", &nexttok); *tok; tok++) {
		if (isspace(*tok))
		    continue;

		switch (st) {
		case LPAREN:
		    if (*tok != '(')
			conferr(linenum, "expected '('");
		    st = FIRST;
		    break;

		case FIRST:
		    cnt = strtoul_nooctal(tok, &ep);
		    if (*ep != ',' && !isspace(*ep))
			conferr(linenum, "invalid number: %s", tok);
		    tok = ep - 1;
		    st = COMMA;
		    break;

		case COMMA:
		    if (*tok != ',')
			conferr(linenum, "expected ','");
		    st = SECOND;
		    break;

		case SECOND:
		    sz = strtoul_nooctal(tok, &ep);
		    if (*ep != ')' && !isspace(*ep))
			conferr(linenum, "invalid number: %s", tok);
		    tok = ep - 1;
		    st = RPAREN;
		    break;

		case RPAREN:
		    if (*tok != ')')
			conferr(linenum, "expected ','");

		    if (blen >= alloc_len) {
			struct yaesu_blocksizes *nb;

			alloc_len += 16;
			nb = malloc(sizeof(*b) * alloc_len);
			if (!nb)
			    conferr(linenum, "out of memory");
			memcpy(nb, b, sizeof(*b) * blen);
			free(b);
			b = nb;
		    }
		    b[blen].nblocks = cnt;
		    b[blen].block_len = sz;
		    blen++;
		    st = LPAREN;
		    break;
		}
	    }
	    
	    if (st != LPAREN)
		conferr(linenum, "end of line in a block spec");

	    if (blen == 0)
		conferr(linenum, "must specify some block lengths");

	    for (i = 0; i < blen - 1; i++) {
		if (b[i].nblocks == 0)
		    conferr(linenum, "all but the last block count must be"
			    " non-zero");
		if (b[i].block_len == 0)
		    conferr(linenum, "llock sizes  must be non-zero");
	    }
	    if (b[i].block_len == 0)
		conferr(linenum, "llock sizes  must be non-zero");

	    b[blen - 1].nblocks = 0; /* make sure end marker is set */
	    r->bsizes = b;

	    goto line_done;
	}

	if (strcmp(tok, "checksum") == 0) {
	    if (r->has_checksum != -1)
		conferr(linenum, "checksum already specified");
	    r->has_checksum = 1;
	    goto line_done;
	}

	if (strcmp(tok, "nochecksum") == 0) {
	    if (r->has_checksum != -1)
		conferr(linenum, "checksum already specified");
	    r->has_checksum = 0;
	    goto line_done;
	}

	if (strcmp(tok, "waitchecksum") == 0) {
	    if (r->waitchecksum != -1)
		conferr(linenum, "waitchecksum already specified");
	    r->waitchecksum = 1;
	    goto line_done;
	}

	if (strcmp(tok, "nowaitchecksum") == 0) {
	    if (r->waitchecksum != -1)
		conferr(linenum, "waitchecksum already specified");
	    r->waitchecksum = 0;
	    goto line_done;
	}

	if (strcmp(tok, "checkblock") == 0) {
	    if (r->has_checkblock != -1)
		conferr(linenum, "checkblock already specified");
	    r->has_checkblock = 1;
	    goto line_done;
	}

	if (strcmp(tok, "nocheckblock") == 0) {
	    if (r->has_checkblock != -1)
		conferr(linenum, "checkblock already specified");
	    r->has_checkblock = 0;
	    goto line_done;
	}

    line_done:
	free(line);
    }

    if (in_radio)
	conferr(linenum, "end of file while still in a radio spec");

    return 0;
}

void
usage(void)
{
    printf("A program to read and write memory data on Yaesu radios.  This\n");
    printf("lets you use the clone mode of the radio to copy the data from\n");
    printf("the radio into a file, and to write the data to a radio from\n");
    printf("a file.\n\n");
    printf("  %s [options] [-d {device}] -r|-w  file\nOptions are:\n",
	   progname);
    printf("  -h, --hash - Print . marks for every block transferred.\n");
    printf("  -v, --verbose - once to get the version, twice to get data.\n");
    printf("  -d, --device - specify the device to use for the radio.\n");
    printf("  -s, --speed - specify the serial baud to use, 9600 by default\n");
    printf("  -i, --ignerr - if an error occurs on read, still save as much"
	   "data as possible.\n");
    printf("  -n, --notermio - don't set up the serial port parameters, useful"
	   "for non-serial ports.\n");
    printf("  -r, --read - Read data from the radio.\n");
    printf("  -w, --write - Write data to the radio.\n");
    printf("  -e, --rcv_echo - Expect transmitted data to be echoed by\n"
	   " the remote end.  Overrides default.\n");
    printf("  -m, --norcv_echo - Do not expect transmitted data to be echoed\n"
	   " by the remote end.\n");
    printf("  -y, --send_echo - Echo all received characters.\n");
    printf("  -c, --checksum - Send/expect a checksum at the end\n");
    printf("  -g, --nochecksum - Do not send/expect a checksum at the end\n");
    printf("  -j, --waitchecksum - Wait for ack before sending checkum\n");
    printf("  -k, --nowaitchecksum - No ack wait before sending checksum\n");
    printf("  -p, --checkblock - Send/expect block checksums\n");
    printf("  -q, --nocheckblock - Do not send/expect block checksums\n");
    printf("  -f, --configdir <file> - Use the given directory for the radio"
	   " configuration instead\nof the default %s\n", YAESU_CONFIGDIR);
}

struct {
    char *sval;
    int val;
} speeds[] = {
    { "9600", B9600 },
    { "19200", B19200 },
    { NULL }
};

int
main(int argc, char *argv[])
{
    int c;
    struct yaesu_data *d = NULL;
    struct yaesu_block *b;
    FILE *f;
    int fd;
    int rv;
    char dummy;
    unsigned int i;

    char *devicename = "/dev/ttyS0";
    char *configdir = YAESU_CONFIGDIR;
    char *filename = NULL;
    int ignerr = 0;
    int dotermios = 1;
    int do_read = 0;
    int do_write = 0;
    int send_echo = 0;
    int recv_echo = -1;
    int do_checksum = -1;
    int do_checkblock = -1;
    int do_waitchecksum = -1;
    char *speed = "9600";
    int bspeed = -1;
    struct termios orig_termios, curr_termios;

    progname = argv[0];

    while (1) {
	c = getopt_long(argc, argv, "?hvd:inrwtemycgf:pqs:",
			long_options, NULL);
	if (c == -1)
	    break;
	switch(c) {
	    case 'v':
		verbose++;
		break;

	    case 'h':
		hash = 1;
		break;

	    case 'd':
		devicename = optarg;
		break;

	    case 'f':
		configdir = optarg;
		break;

	    case 'i':
		ignerr = 1;
		break;

	    case 'n':
		dotermios = 0;
		break;

	    case 'r':
		do_read = 1;
		break;

	    case 'w':
		do_write = 1;
		break;

	    case 'e':
		recv_echo = 1;
		break;

	    case 'm':
		recv_echo = 0;
		break;

	    case 'c':
		do_checksum = 1;
		break;

	    case 'g':
		do_checksum = 0;
		break;

	    case 'j':
		do_waitchecksum = 1;
		break;

	    case 'k':
		do_waitchecksum = 0;
		break;

	    case 'y':
		send_echo = 1;
		break;

	    case 'p':
		do_checkblock = 1;
		break;

	    case 'q':
		do_checkblock = 0;
		break;

	    case 's':
		speed = optarg;
		break;
		
	    case '?':
		fprintf(stderr, "unknown argument: %c\n", optopt);
		usage();
		exit(0);
		break;

	    case ':':
		fprintf(stderr, "missing argument\n");
		usage();
		exit(1);
		break;
	}
    }

    if (verbose)
	printf("%s - Yaesu radio reader %s\n", progname, version);

    if (optind + 1 != argc) {
	if (verbose)
	    exit(0);
	fprintf(stderr, "Missing filename.\n");
	usage();
	exit(1);
    }
    filename = argv[optind];

    read_yaesu_config(configdir);
 
    if ((do_read + do_write) > 1) {
	fprintf(stderr, "Can only specify one of -r and -w.\n");
	exit(1);
    }

    if (do_read) {
	f = fopen(filename, "w");
	if (!f) {
	    fprintf(stderr, "Unable to open outfile: %s (%d)\n", strerror(errno), errno);
	    exit(1);
	}
    } else if (do_write) {
	f = fopen(filename, "r");
	if (!f) {
	    fprintf(stderr, "Unable to open infile: %s (%d)\n", strerror(errno), errno);
	    exit(1);
	}
    } else {
	fprintf(stderr, "Must specify one of -r or -w.\n");
	exit(1);
    }

    if ((fd = open(devicename, O_RDWR | O_NDELAY)) < 0) {
	fclose(f);
	perror(devicename);
	exit(1);
    }

    for (i = 0; speeds[i].sval; i++) {
	if (strcmp(speeds[i].sval, speed) == 0) {
	    bspeed = speeds[i].val;
	    break;
	}
    }
    if (bspeed == -1) {
	fprintf(stderr, "Unknown speed, valid speeds are:");
	for (i = 0; speeds[i].sval; i++)
	    fprintf(stderr, " %s", speeds[i].sval);
	fprintf(stderr, "\n");
	goto out_err;
    }

    if (dotermios) {
	if (tcgetattr(fd, &orig_termios) == -1) {
	    fprintf(stderr, "error getting tty attributes %s(%d)\n",
		    strerror(errno), errno);
	    goto out_err;
	}

	curr_termios = orig_termios;

	cfsetospeed(&curr_termios, bspeed);
	cfsetispeed(&curr_termios, bspeed);
	cfmakeraw(&curr_termios);
	/* two stop bits, ignore handshake, make sure rx enabled */
	curr_termios.c_cflag |= (CSTOPB | CLOCAL | CREAD);

	if (tcsetattr(fd, TCSANOW, &curr_termios) == -1) {
	    fprintf(stderr, "error setting tty attributes %s(%d)\n",
		    strerror(errno), errno);
	    goto out_err;
	}
    }

    /* Flush the input buffer */
    while(read(fd, &dummy, 1) > 0)
	;

    if (recv_echo == -1) {
	/* Test for echo */
	rv = write(fd, "A", 1);
	if (rv <= 0) {
	    fprintf(stderr, "Error testing echo, could not write to"
		    " device: %s (%d)\n", strerror(errno), errno);
	    exit(1);
	}

	usleep(200000);
	rv = read(fd, &dummy, 1);
	if (rv < 0) {
	    if (errno == EAGAIN)
		rv = 0;
	    else {
		fprintf(stderr, "Error testing echo, could not read from"
			" device: %s (%d)\n", strerror(errno), errno);
		exit(1);
	    }
	}
	if (rv == 0)
	    recv_echo = 0;
	else if (dummy == 'A')
	    recv_echo = 1;
	else {
	    fprintf(stderr, "Error testing echo, received character did"
		    " not match sent character.\n");
	    exit(1);
	}
	if (verbose)
	    printf("Receive echo is %s\n", recv_echo ? "on" : "off");
    }

    d = alloc_yaesu_data(fd, fd, do_read, send_echo, recv_echo, do_checksum,
			 do_checkblock, do_waitchecksum);
    if (!d) {
	fclose(f);
	close(fd);
	fprintf(stderr, "Out of memory\n");
	exit(1);
    }

    if (do_write) {
	unsigned int hsize, bsize;
	unsigned char *header, *block;
	unsigned int len;
	unsigned int readoff = 0, readsub = 0;
	unsigned int blocknum = 1;

	hsize = max_header_size();
	header = malloc(hsize);
	if (!header) {
	    fprintf(stderr, "Out of memory\n");
	    goto out_err;
	}

	len = fread(header, 1, hsize, f);
	if (len == 0) {
	    fprintf(stderr, "Unable to read file header\n");
	    goto out_err;
	}

	for (i = 1; i <= len; i++) {
	    if (check_yaesu_type(d, header, i, 0))
		goto found;
	}
	fprintf(stderr, "Unable to find radio type\n");
	exit(1);
    found:

	add_yaesu_block(d, header, i);
	len -= i;

	bsize = max_blocksize(d);
	block = malloc(bsize);
	if (!block) {
	    fprintf(stderr, "Out of memory\n");
	    goto out_err;
	}
	if (d->has_checkblock) {
	    bsize -= 2; /* The blocknum and checksum won't be in the file. */
	    readoff = 1; /* Leave space for the blocknum first */
	    readsub = 2; /* Won't read the blocknum or checksum */
	}

	memcpy(block + readoff, header + i, len);
	free(header);
	len += fread(block + len + readoff, 1, d->block_len - len - readsub, f);
	if (len != (d->block_len - readsub)) {
	    fprintf(stderr, "Not enough data in file\n");
	    goto out_err;
	}
	if (d->has_checkblock) {
	    unsigned int sum;
	    unsigned int u;

	    block[0] = blocknum;
	    len++;
	    sum = 0;
	    for (u = 1; u < len; u++)
		sum += block[u];
	    block[len] = sum;
	    len++;
	}
	blocknum++;
	add_yaesu_block(d, block, len);
	    
	while (d->data_count < d->expect_len) {
	    len = fread(block + readoff, 1, d->block_len - readsub, f);
	    if (len != (d->block_len - readsub)) {
		fprintf(stderr, "Not enough data in file: %d\n", len);
		goto out_err;
	    }

	    if (d->has_checkblock) {
		unsigned int sum;
		unsigned int u;

		block[0] = blocknum;
		len++;
		sum = 0;
		for (u = 1; u < len; u++)
		    sum += block[u];
		block[len] = sum;
		len++;
	    }
	    blocknum++;
	    add_yaesu_block(d, block, len);
	}
	len = fread(block, 1, 1, f);
	if (len == 1) {
	    fprintf(stderr, "Extra data at the end of the file, probably not"
		    " a valid file\n");
	    goto out_err;
	}
    }

    if (do_write) {
	char c;
	printf("Put the radio in rx mode and press enter");
	fflush(stdout);
	rv = read(0, &c, 1);
	while (read(fd, &c, 1) > 0)
	    ;
	rv = yaesu_start_write(d);
	if (rv) {
	    fprintf(stderr, "error starting write %s(%d)\n", strerror(rv), rv);
	    goto out_err;
	}
    } else
	printf("Start transmission from the radio\n");

    rv = 0;
    while (!yaesu_is_done(d)) {
	fd_set rfds, wfds;
	struct timeval tv;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	FD_SET(fd, &rfds);
	yaesu_get_timeout(d, &tv);
	if (yaesu_check_write(d))
	    FD_SET(fd, &wfds);
	rv = select(fd + 1, &rfds, &wfds, NULL, &tv);
	if (rv == 0) {
	    /* Timeout */
	    rv = handle_yaesu_timeout(d);
	    if (rv) {
		printf("Error: %s\n", strerror(rv));
		goto out;
	    }
	} else if (rv > 0) {
	    /* Got data */
	    if (FD_ISSET(fd, &rfds)) {
		rv = handle_yaesu_data(d);
		if (rv) {
		    printf("Error handling read data: %s\n", strerror(rv));
		    goto out;
		}
	    }
	    if (FD_ISSET(fd, &wfds)) {
		rv = handle_yaesu_write_ready(d);
		if (rv) {
		    printf("Error handling write data: %s\n", strerror(rv));
		    goto out;
		}
	    }
	} else {
	    perror("select");
	    goto out;
	}
    }

 out:
    if (hash || d->timeout_mode)
	printf("\n");
    printf("Transferred %d characters\n", d->data_count);

    if (!rv || ignerr) {
	b = d->head.next;
	while (b != &d->head) {
	    fwrite(b->buff, b->len, 1, f);
	    b = b->next;
	}
    }

 out_err:

    if (dotermios)
	tcsetattr(fd, TCSANOW, &orig_termios);

    fclose(f);
    close(fd);

    if (d)
	free_yaesu_data(d);

    return 0;
}
