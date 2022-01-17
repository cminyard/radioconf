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
#include <getopt.h>
#include <unistd.h> /* FIXME - for usleep. */
#include <sys/ioctl.h> /* FIXME - for ioctl. */
#ifdef __APPLE__
#include <sys/malloc.h>
#else
#include <malloc.h>
#endif
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <gensio/gensio.h>

#ifndef RADIO_CONFIGDIR
#define RADIO_CONFIGDIR "/etc/radioconf"
#endif

char *version = PACKAGE_VERSION;

static struct option long_options[] = {
    {"help",	 0, NULL, '?'},
    {"hash",	 0, NULL, 'h'},
    {"verbose",  0, NULL, 'v'},
    {"file",	 1, NULL, 'f'},
    {"ignerr",	 0, NULL, 'I'},
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
    {"delayack", 0, NULL, 'l'},
    {"nodelayack",0, NULL, 'n'},
    {"chunksize",1, NULL, 'a'},
    {"waitchunk",1, NULL, 'b'},
    {"configdir",0, NULL, 'F'},
    {"prewritedelay", 1, NULL, 't'},
    {"noendecho", 0, NULL, 'u'},
    {"csumdelay", 1, NULL, 'x'},
    {NULL,	 0, NULL, 0}
};
char *progname = NULL;
int verbose = 0;
int hash = 0;

#define YAESU_TIMEOUT(d, s, u) \
    do {							\
	d->timeout.secs = s; d->timeout.nsecs = u * 1000;	\
    } while(0)
#define YAESU_TEST_TIMEOUT(d) YAESU_TIMEOUT(d, 1, 0)
#define YAESU_CHAR_TIMEOUT(d) YAESU_TIMEOUT(d, 5, 0)
#define YAESU_TIMING_TIMEOUT(d) YAESU_TIMEOUT(d, 0, 200000)
#define YAESU_WAITWRITE_TIMEOUT(d) YAESU_TIMEOUT(d, 0, 5000)
#define YAESU_CSUMDELAY_TIMEOUT(d) YAESU_TIMEOUT(d, 0, d->csumdelay)
#define YAESU_WAITCHUNK_TIMEOUT(d) YAESU_TIMEOUT(d, 0, d->waitchunk)
#define YAESU_START_TIMEOUT(d) YAESU_TIMEOUT(d, 10, 0)
#define YAESU_MAX_RETRIES 5

enum yaesu_read_state {
    YAESU_STATE_WAITFIRSTBLOCK,
    YAESU_STATE_INFIRSTBLOCK,
    YAESU_STATE_WAITBLOCK,
    YAESU_STATE_INBLOCK,
    YAESU_STATE_DELAYCSUM,
    YAESU_STATE_DELAYACK,
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
    int err;
    int is_read;
    enum yaesu_read_state state;
    struct gensio *io;
    struct gensio_os_funcs *o;
    gensio_time timeout;
    unsigned int pos;
    unsigned int rpos;
    unsigned int retries;
    struct yaesu_block head;
    struct yaesu_block *curr_block;
    unsigned int data_count;
    unsigned int block_count;
    int send_echo;
    int recv_echo;
    int noendecho;
    unsigned int expect_len;
    int timeout_mode;
    unsigned int csum;
    unsigned int block_len;
    struct yaesu_blocksizes *bsizes;
    unsigned int bsize_left;
    unsigned int curr_bsize;
    unsigned char write_buf[65536];
    unsigned int write_start;
    unsigned int write_len;
    int waitchunk;
    int csumdelay;
    int chunksize;
    enum { CHUNK_NOWAIT, CHUNK_CHECK, CHUNK_DELAY } waiting_chunk_done;
    int has_checksum;
    int has_checkblock;
    int waitchecksum;
    int delayack;
    int prewritedelay;
};

#define DEFAULT_CHUNKSIZE 0
#define DEFAULT_WAITCHUNK 0
#define DEFAULT_CSUMDELAY 0

const unsigned char testbuf[8] = { '0', '1', '2', '3', '4', '5', '6', '7' };
const unsigned char ack[1] = { 0x06 };

static void
do_vlog(struct gensio_os_funcs *f, enum gensio_log_levels level,
	const char *log, va_list args)
{
    fprintf(stderr, "gensio %s log: ", gensio_log_level_to_str(level));
    vfprintf(stderr, log, args);
    fprintf(stderr, "\n");
}

struct yaesu_data *
alloc_yaesu_data(struct gensio_os_funcs *o, struct gensio *io, int is_read,
		 int send_echo, int recv_echo, int noendecho, int has_checksum,
		 int has_checkblock, int waitchecksum, int chunksize,
		 int waitchunk, int delayack, int prewritedelay, int csumdelay)
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
    d->io = io;
    d->o = o;
    d->write_start = 0;
    d->write_len = 0;
    d->chunksize = chunksize;
    d->waitchunk = waitchunk;
    d->csumdelay = csumdelay;
    d->delayack = 1;
    d->waiting_chunk_done = CHUNK_NOWAIT;
    d->send_echo = send_echo;
    d->recv_echo = recv_echo;
    d->noendecho = noendecho;
    d->bsizes = NULL;
    d->timeout_mode = 0;
    d->has_checksum = has_checksum;
    d->has_checkblock = has_checkblock;
    d->waitchecksum = waitchecksum;
    d->prewritedelay = prewritedelay;

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
yaesu_get_timeout(struct yaesu_data *d, gensio_time *time)
{
    *time = d->timeout;
}

int
yaesu_is_done(struct yaesu_data *d)
{
    return d->state == YAESU_STATE_DONE || d->err;
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
    int recv_echo;
    int send_echo;
    int noendecho;
    int has_checksum;
    int has_checkblock;
    int waitchecksum;
    int chunksize;
    int waitchunk;
    int csumdelay;
    int delayack;
    int prewritedelay;
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
		d->recv_echo = r->recv_echo;
	    if (d->send_echo < 0)
		d->send_echo = r->send_echo;
	    if (d->noendecho < 0)
		d->noendecho = r->noendecho;
	    if (d->has_checksum < 0)
		d->has_checksum = r->has_checksum;
	    if (d->has_checkblock < 0)
		d->has_checkblock = r->has_checkblock;
	    if (d->waitchecksum < 0)
		d->waitchecksum = r->waitchecksum;
	    if (d->chunksize < 0)
		d->chunksize = r->chunksize;
	    if (d->waitchunk < 0)
		d->waitchunk = r->waitchunk;
	    if (d->csumdelay < 0)
		d->csumdelay = r->csumdelay;
	    if (d->delayack < 0)
		d->delayack = r->delayack;
	    if (d->prewritedelay < 0)
		d->prewritedelay = r->prewritedelay;
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
	if (d->send_echo < 0)
	    d->send_echo = 0;
	if (d->noendecho < 0)
	    d->noendecho = 0;
	if (d->has_checksum < 0)
	    d->has_checksum = 0;
	if (d->has_checkblock < 0)
	    d->has_checkblock = 0;
	if (d->waitchecksum < 0)
	    d->waitchecksum = 1;
	if (d->waitchunk < 0)
	    d->waitchunk = DEFAULT_WAITCHUNK;
	if (d->csumdelay < 0)
	    d->csumdelay = DEFAULT_CSUMDELAY;
	if (d->chunksize < 0)
	    d->chunksize = DEFAULT_CHUNKSIZE;
	if (d->delayack < 0)
	    d->delayack = 1;
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
	    return GE_NOMEM;
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
	return GE_NOMEM;
    b->buff = malloc(size);
    if (!b->buff) {
	free(b);
	return GE_NOMEM;
    }
    memcpy(b->buff, data, size);
    b->len = size;
    d->data_count += size;
    return 0;
}

int
yaesu_write(struct yaesu_data *d, const unsigned char *data, unsigned int len,
	    bool *written)
{
    unsigned int end;
    int rv;
    unsigned int total_written = 0;

    if (d->prewritedelay > 0)
	usleep(d->prewritedelay);

    if (len + d->write_len > sizeof(d->write_buf)) {
	*written = false;
	return 0;
    }
    *written = true;

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
	gensiods written;

	if (end > d->write_start) {
	write_nowrap:
	    if ((d->chunksize > 0)
		&& (total_written + d->write_len > (unsigned int) d->chunksize))
		to_write = d->chunksize - total_written;
	    else
		to_write = d->write_len;
	    rv = gensio_write(d->io, &written, d->write_buf + d->write_start,
			      to_write, NULL);
	    if (rv)
		return rv;
	    total_written += written;
	    if (written > 0 && verbose > 2) {
		int i;
		gensio_time time;

		gensio_os_funcs_get_monotonic_time(d->o, &time);
		printf("Write(2: %ld:%6.6ld):", time.secs, (long) time.nsecs);
		for (i = 0; i < rv; i++)
		    printf(" %2.2x", d->write_buf[d->write_start+i]);
		printf("\n");
		fflush(stdout);
	    }

	    d->write_len -= written;
	    d->write_start += written;
	    if ((d->chunksize > 0)
		&& (total_written >= (unsigned int) d->chunksize))
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
	    if ((d->chunksize > 0)
		&& (total_written + wrc > (unsigned int) d->chunksize))
		to_write = d->chunksize - total_written;
	    else
		to_write = d->write_len;
	    rv = gensio_write(d->io, &written,
			      d->write_buf + d->write_start, to_write, NULL);
	    if (rv)
		return rv;
	    total_written += written;
	    if (written > 0 && verbose > 2) {
		int i;
		printf("Write(2):");
		for (i = 0; i < rv; i++)
		    printf(" %2.2x", d->write_buf[d->write_start+i]);
		printf("\n");
		fflush(stdout);
	    }
	    d->write_len -= written;
	    d->write_start += written;
	    if (d->write_start >= sizeof(d->write_buf)) {
		/* We wrote it all, move to the beginning of the buffer. */
		d->write_start = 0;
		if (d->write_len > 0)
		    /* Finish up the write. */
		    goto write_nowrap;
	    } else if ((d->chunksize > 0)
		       && (total_written >= (unsigned int) d->chunksize))
		goto out_delay;
	    else
		goto not_all_written;
	}
    }

    gensio_set_write_callback_enable(d->io, false);
    return 0;

 out_delay:
    gensio_set_write_callback_enable(d->io, false);
    YAESU_WAITWRITE_TIMEOUT(d);
    d->waiting_chunk_done = CHUNK_CHECK;
    return 0;

 not_all_written:
    gensio_set_write_callback_enable(d->io, true);
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
	return GE_INVAL;
    if (b->buff[0] != (d->block_count - 1))
	return GE_INVAL;
    sum = 0;
    for (i = 1; i < b->len - 1; i++) {
	sum += b->buff[i];
	b->buff[i-1] = b->buff[i];
    }
    b->len -= 2; /* We remove the block number and the checksum */
    if (b->buff[b->len + 1] != (sum & 0xff))
	return GE_INVAL;
    return 0;
}

int
handle_yaesu_read_data(struct yaesu_data *d,
		       unsigned char *buf, gensiods buflen)
{
    int rv, err;
    unsigned int i;
    struct yaesu_block *b;
    bool written;

    if (verbose > 1) {
	gensio_time time;

	gensio_os_funcs_get_monotonic_time(d->o, &time);
	printf("Read (%ld:%6.6ld):", time.secs, (long) time.nsecs);
	for (i = 0; i < buflen; i++)
	    printf(" %2.2x", buf[i]);
	printf("\n");
	fflush(stdout);
    }

    if (d->send_echo) {
	/* Radio echos everything received. */
	int err = yaesu_write(d, buf, buflen, &written);
	if (err)
	    return err;
	if (!written)
	    return GE_TOOBIG;
    }

    switch(d->state) {
    case YAESU_STATE_WAITFIRSTBLOCK:
	d->state = YAESU_STATE_INFIRSTBLOCK;
	b = yaesu_new_block(d);
	if (!b)
	    return GE_NOMEM;
	YAESU_CHAR_TIMEOUT(d);
	/* FALLTHROUGH */

    case YAESU_STATE_INFIRSTBLOCK:
	b = d->head.prev;

	if ((buflen + b->len) > MAX_BLOCK_SIZE)
	    return GE_INVAL;
	err = append_yaesu_data(d, b, buf, buflen);
	if (err)
	    return err;
	d->data_count += buflen;
	for (i = 0; i < buflen; i++)
	    d->csum += buf[i];

	if (check_yaesu_type(d, b->buff, b->len, 0)) {
	    if (hash) {
		printf(".");
		fflush(stdout);
	    }
	    d->state = YAESU_STATE_WAITBLOCK;
	    rv = yaesu_write(d, ack, 1, &written);
	    if (rv)
		return rv;
	    if (!written)
		return GE_TOOBIG;
	}
	break;

    case YAESU_STATE_WAITBLOCK:
	if (d->recv_echo) {
	    if (buf[0] != ack[0])
		return GE_INVAL;
	    buf++;
	    buflen--;
	}
	d->state = YAESU_STATE_INBLOCK;
	b = yaesu_new_block(d);
	if (!b)
	    return GE_NOMEM;
	if (buflen == 0)
	    break;
	/* FALLTHROUGH */

    case YAESU_STATE_INBLOCK:
	b = d->head.prev;

	if (!d->timeout_mode && ((buflen + b->len) > d->block_len))
	    return GE_INVAL;
	err = append_yaesu_data(d, b, buf, buflen);
	if (err)
	    return err;
	d->data_count += buflen;
	for (i = 0; i < buflen; i++)
	    d->csum += buf[i];

	if (d->data_count > d->expect_len)
	    return GE_INVAL;
	else if (d->data_count == d->expect_len) {
	    int doack = 1;
	    rv = yaesu_check_block(d, b);
	    if (rv)
		return rv;
	    if (d->has_checksum)
		d->state = YAESU_STATE_WAITCSUM;
	    else if (d->delayack) {
		/* Some radios are picky and want a delay before sending the
		   last ack. */
		YAESU_WAITWRITE_TIMEOUT(d);
		d->state = YAESU_STATE_DELAYACK;
		doack = 0;
	    } else
		d->state = YAESU_STATE_DONE;
	    if (doack) {
		rv = yaesu_write(d, ack, 1, &written);
		if (rv)
		    return rv;
		if (!written)
		    return GE_TOOBIG;
	    }
	} else if (!d->timeout_mode && (b->len == d->block_len)) {
	    if (hash) {
		printf(".");
		fflush(stdout);
	    }
	    rv = yaesu_check_block(d, b);
	    if (rv)
		return rv;
	    d->state = YAESU_STATE_WAITBLOCK;
	    rv = yaesu_write(d, ack, 1, &written);
	    if (rv)
		return rv;
	    if (!written)
		return GE_TOOBIG;
	}
	break;

    case YAESU_STATE_WAITCSUM:
	if (d->recv_echo && !d->noendecho) {
	    if (buf[0] != ack[0])
		return GE_INVAL;
	    buf++;
	    buflen--;
	}
	d->state = YAESU_STATE_CSUM;
	if (buflen == 0)
	    break;
	/* FALLTHROUGH */

    case YAESU_STATE_CSUM:
	if ((d->csum & 0xff) != buf[0])
	    /* Checksum failure. */
	    return GE_INVAL;
	if (d->delayack) {
	    /* Some radios are picky and want a delay before sending the
	       last ack. */
	    YAESU_WAITWRITE_TIMEOUT(d);
	    d->state = YAESU_STATE_DELAYACK;
	} else {
	    rv = yaesu_write(d, ack, 1, &written);
	    if (rv)
		return rv;
	    if (!written)
		return GE_TOOBIG;
	}
	break;

    case YAESU_STATE_CSUM_WAITACK:
    case YAESU_STATE_DONE:
    case YAESU_STATE_DELAYCSUM:
    case YAESU_STATE_DELAYACK:
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
    bool written;

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
	return GE_TIMEDOUT;

    case YAESU_STATE_DELAYACK:
	d->state = YAESU_STATE_DONE;
	rv = yaesu_write(d, ack, 1, &written);
	if (rv)
	    return rv;
	if (!written)
	    return GE_TOOBIG;
	return 0;

    case YAESU_STATE_CSUM_WAITACK:
    case YAESU_STATE_DONE:
    case YAESU_STATE_DELAYCSUM:
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
	rv = yaesu_write(d, ack, 1, &written);
	if (rv)
	    return rv;
	if (!written)
	    return GE_TOOBIG;
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
	rv = yaesu_write(d, ack, 1, &written);
	if (rv)
	    return rv;
	if (!written)
	    return GE_TOOBIG;
	break;

    case YAESU_STATE_WAITBLOCK:
	/* Must be end of data.  The last block will be the checksum. */
	b = d->head.prev;
	if (b->len == 0) {
	    printf("Last data block was 0 bytes, not a checksum?\n");
	    return GE_INVAL;
	}
	b->len--;
	d->csum -= b->buff[b->len];
	if (d->has_checksum && (d->csum & 0xff) != b->buff[b->len])
	    /* Checksum failure. */
	    return GE_INVAL;
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
    bool written;

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
    rv = yaesu_write(d, b->buff + d->pos, to_write, &written);
    if (rv)
	return rv;
    if (written) {
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
    bool written;

    if (d->is_read) {
	rv = yaesu_write(d, NULL, 0, &written);
	if (rv)
	    return rv;
	if (!written)
	    return GE_TOOBIG;
    }

    if (d->state == YAESU_STATE_WAITCSUM) {
	unsigned char cs[1];

	cs[0] = d->csum;
	rv = yaesu_write(d, cs, 1, &written);
	if (rv)
	    return rv;
	if (!written)
	    return 0;
	if (!d->waitchecksum)
	    d->state = YAESU_STATE_DONE;
	else if (!d->recv_echo)
	    d->state = YAESU_STATE_CSUM_WAITACK;
	else
	    d->state = YAESU_STATE_CSUM;
	return 0;
    }

    b = d->curr_block;
    to_write = b->len - d->pos;
    rv = yaesu_write(d, b->buff + d->pos, to_write, &written);
    if (rv)
	return rv;
    if (!written)
	return 0;
    d->pos += to_write;
    d->data_count += to_write;
    return 0;
}

int
handle_yaesu_write_data(struct yaesu_data *d,
			unsigned char *buf, gensiods buflen)
{
    struct yaesu_block *b;
    int rv;
    unsigned int i;

    if (verbose > 1) {
	gensio_time time;

	gensio_os_funcs_get_monotonic_time(d->o, &time);
	printf("Read (%d: %ld:%6.6ld):", d->state, time.secs, (long)time.nsecs);
	for (i = 0; i < buflen; i++)
	    printf(" %2.2x", buf[i]);
	printf("\n");
	fflush(stdout);
    }

    if (d->send_echo) {
	bool written;
	int err = yaesu_write(d, buf, buflen, &written);
	if (err)
	    return err;
	if (!written)
	    return GE_TOOBIG;
    }

    if (d->state == YAESU_STATE_DONE)
	return 0;

    if (d->state == YAESU_STATE_CSUM) {
	if (buf[0] != (d->csum & 0xff))
	    return GE_INVAL;
	d->state = YAESU_STATE_CSUM_WAITACK;
	buflen--;
	if (buflen == 0)
	    return 0;
	buflen++;
    }

    if (d->state == YAESU_STATE_CSUM_WAITACK) {
	bool written;

	rv = yaesu_write(d, ack, 1, &written);
	if (rv)
	    return rv;
	if (!written)
	    return GE_TOOBIG;
	d->state = YAESU_STATE_DONE;
	return 0;
    }

    b = d->curr_block;
    if (d->recv_echo && (d->rpos < b->len)) {
	b = d->curr_block;
	for (i = 0; (i < buflen) && (d->rpos < b->len); i++, d->rpos++) {
	    if (buf[i] != b->buff[d->rpos])
		return GE_INVAL;
	}
	if (d->rpos < b->len)
	    /* More echos to receive, so just return. */
	    return 0;

	buflen -= i;
	buf += i;
	/* Might have the ack, so go on. */
    }

    b = b->next;
    if (d->has_checksum && !d->waitchecksum && (b == &d->head))
	/* Ugly, but if we don't wait for the ack before sending the
	   checksum, then send it now. */
	goto send_checksum;

    if (buflen == 0)
	return 0;

    /* We should have the ack. */
    if (buflen != 1)
	return GE_INVAL;
    if (buf[0] != ack[0])
	return GE_INVAL;
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
    if (!d->waitchecksum) {
	d->state = YAESU_STATE_DELAYCSUM;
	YAESU_CSUMDELAY_TIMEOUT(d);
	return 0;
    }

    {
	unsigned char cs[1];
	bool written;

	cs[0] = d->csum;
	rv = yaesu_write(d, cs, 1, &written);
	if (rv)
	    return rv;
	if (!written)
	    d->state = YAESU_STATE_WAITCSUM;
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

    if (d->state == YAESU_STATE_DELAYCSUM) {
	d->state = YAESU_STATE_WAITCSUM;
	return 0;
    }

    if (d->waiting_chunk_done == CHUNK_CHECK) {
	int left;
	int fd;
	gensiods len = sizeof(fd);

	/* FIXME - this is a hack. */
	rv = gensio_control(d->io, 0, true, GENSIO_CONTROL_REMOTE_ID,
			    (char *) &fd, &len);

	rv = ioctl(fd, TIOCOUTQ, &left);
	if (rv < 0)
	    return gensio_os_err_to_err(d->o, rv);
	if (left == 0) {
	    YAESU_WAITCHUNK_TIMEOUT(d);
	    d->waiting_chunk_done = CHUNK_DELAY;
	}
	return 0;
    } else if (d->waiting_chunk_done == CHUNK_DELAY) {
	YAESU_CHAR_TIMEOUT(d);
	d->waiting_chunk_done = CHUNK_NOWAIT;
	return 0;
    }
    return GE_TIMEDOUT;
}

int
handle_yaesu_data(struct yaesu_data *d, unsigned char *buf, gensiods buflen)
{
    if (d->is_read)
	return handle_yaesu_read_data(d, buf, buflen);
    else
	return handle_yaesu_write_data(d, buf, buflen);
}

int
handle_yaesu_timeout(struct yaesu_data *d)
{
    if (d->is_read)
	return handle_yaesu_read_timeout(d);
    else
	return handle_yaesu_write_timeout(d);
}

/* FIXME - need a better for error then -err. */
int
get_one_line(FILE *f, char **oline, unsigned int *linenum)
{
    char *line = malloc(80);
    int alloc_len = 80;
    int len;

    if (!line)
	return -GE_NOMEM;

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
	    return -GE_NOMEM;
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
	    return -GE_NOMEM;
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
      return GE_NOMEM;

    strcpy(configfile, configdir);
    strcat(configfile, "/");
    strcat(configfile, fname);

    f = fopen(configfile, "r");
    if (!f) {
	perror(configfile);
	exit(1);
    }
    free(configfile);

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
	    r->recv_echo = -1;
	    r->send_echo = -1;
	    r->has_checksum = -1;
	    r->has_checkblock = -1;
	    r->waitchecksum = -1;
	    r->chunksize = -1;
	    r->waitchunk = -1;
	    r->csumdelay = -1;
	    r->delayack = -1;
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
	    /* Don't default the recv_echo. */
	    if (r->send_echo == -1)
		r->send_echo = 0;
	    if (r->has_checksum == -1)
		r->has_checksum = 0;
	    if (r->has_checkblock == -1)
		r->has_checkblock = 0;
	    if (r->waitchecksum == -1)
		r->waitchecksum = 1;
	    if (r->waitchunk == -1)
		r->waitchunk = DEFAULT_WAITCHUNK;
	    if (r->csumdelay == -1)
		r->csumdelay = DEFAULT_CSUMDELAY;
	    if (r->chunksize == -1)
		r->chunksize = DEFAULT_CHUNKSIZE;
	    if (r->delayack == -1)
		r->delayack = 1;

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
			conferr(linenum, "Out of memory");
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

	if (strcmp(tok, "chunksize") == 0) {
	    if (r->chunksize >= 0)
		conferr(linenum, "Chunk size already specified");

	    tok = strtok_r(NULL, " \t", &nexttok);
	    if (!tok)
		conferr(linenum, "Expected number");

	    r->chunksize = strtoul_nooctal(tok, &ep);
	    if (*ep)
		conferr(linenum, "Invalid chunk size");

	    goto line_done;
	}

	if (strcmp(tok, "waitchunk") == 0) {
	    if (r->waitchunk >= 0)
		conferr(linenum, "Chunk wait already specified");

	    tok = strtok_r(NULL, " \t", &nexttok);
	    if (!tok)
		conferr(linenum, "Expected number");

	    r->waitchunk = strtoul_nooctal(tok, &ep);
	    if (*ep)
		conferr(linenum, "Invalid chunk wait");

	    goto line_done;
	}

	if (strcmp(tok, "csumdelay") == 0) {
	    if (r->csumdelay >= 0)
		conferr(linenum, "Checksum wait already specified");

	    tok = strtok_r(NULL, " \t", &nexttok);
	    if (!tok)
		conferr(linenum, "Expected number");

	    r->csumdelay = strtoul_nooctal(tok, &ep);
	    if (*ep)
		conferr(linenum, "Invalid checksum wait");

	    goto line_done;
	}

	if (strcmp(tok, "recv_echo") == 0) {
	    if (r->recv_echo != -1)
		conferr(linenum, "recv_echo already specified");
	    r->recv_echo = 1;
	    goto line_done;
	}

	if (strcmp(tok, "send_echo") == 0) {
	    if (r->send_echo != -1)
		conferr(linenum, "send_echo already specified");
	    r->send_echo = 1;
	    goto line_done;
	}

	if (strcmp(tok, "norecv_echo") == 0) {
	    if (r->recv_echo != -1)
		conferr(linenum, "recv_echo already specified");
	    r->recv_echo = 0;
	    goto line_done;
	}

	if (strcmp(tok, "nosend_echo") == 0) {
	    if (r->send_echo != -1)
		conferr(linenum, "send_echo already specified");
	    r->send_echo = 0;
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

	if (strcmp(tok, "delayack") == 0) {
	    if (r->delayack != -1)
		conferr(linenum, "delayack already specified");
	    r->delayack = 1;
	    goto line_done;
	}

	if (strcmp(tok, "nodelayack") == 0) {
	    if (r->delayack != -1)
		conferr(linenum, "delayack already specified");
	    r->delayack = 0;
	    goto line_done;
	}

	if (strcmp(tok, "noendecho") == 0) {
	    r->noendecho = 1;
	}

	if (strcmp(tok, "prewritedelay") == 0) {
	    if (r->prewritedelay)
		conferr(linenum, "prewritedelay already specified");

	    tok = strtok_r(NULL, " \t", &nexttok);
	    if (!tok)
		conferr(linenum, "Expected number");

	    r->prewritedelay = strtoul_nooctal(tok, &ep);
	    if (*ep)
		conferr(linenum, "Invalid prewritedelay");

	    goto line_done;
	}

    line_done:
	free(line);
    }

    if (in_radio)
	conferr(linenum, "end of file while still in a radio spec");

    fclose(f);

    return 0;
}

static int
radio_event(struct gensio *io, void *user_data, int event, int err,
	    unsigned char *buf, gensiods *buflen,
	    const char *const *auxdata)
{
    struct yaesu_data *d = user_data;

    if (err) {
	if (!d->err)
	    d->err = err;
	fprintf(stderr, "Read error: %s\n", gensio_err_to_str(err));
    }
    if (d->err) {
	gensio_set_read_callback_enable(d->io, false);
	gensio_set_write_callback_enable(d->io, false);
	return 0;
    }

    switch (event) {
    case GENSIO_EVENT_READ:
	handle_yaesu_data(d, buf, *buflen);
	return 0;

    case GENSIO_EVENT_WRITE_READY:
	handle_yaesu_write_ready(d);
	return 0;

    default:
	return GE_NOTSUP;
    }
}

void
usage(void)
{
    printf("A program to read and write memory data on Yaesu radios.  This\n");
    printf("lets you use the clone mode of the radio to copy the data from\n");
    printf("the radio into a file, and to write the data to a radio from\n");
    printf("a file.\n\n");
    printf("  %s [options] -r|-w <gensio>\nOptions are:\n",
	   progname);
    printf("  -h, --hash - Print . marks for every block transferred.\n");
    printf("  -v, --verbose - once to get the version, twice to get data.\n");
    printf("  -f, --file <filename> - The file to read/write.\n");
    printf("  -I, --ignerr - if an error occurs on read, still save as much"
	   "data as possible.\n");
    printf("  -r, --read - Read data from the radio.\n");
    printf("  -w, --write - Write data to the radio.\n");
    printf("  -e, --rcv_echo - Expect transmitted data to be echoed by\n"
	   " the remote end.  Overrides default.\n");
    printf("  -m, --norcv_echo - Do not expect transmitted data to be echoed\n"
	   " by the remote end.\n");
    printf("  -u, --noendecho - No echo for the final sent ack\n");
    printf("  -y, --send_echo - Echo all received characters.\n");
    printf("  -c, --checksum - Send/expect a checksum at the end\n");
    printf("  -g, --nochecksum - Do not send/expect a checksum at the end\n");
    printf("  -j, --waitchecksum - Wait for ack before sending checkum\n");
    printf("  -k, --nowaitchecksum - No ack wait before sending checksum\n");
    printf("  -p, --checkblock - Send/expect block checksums\n");
    printf("  -q, --nocheckblock - Do not send/expect block checksums\n");
    printf("  -a, --chunksize <size> - Send data in size blocks and wait\n");
    printf("  -b, --waitchunk <usec> - wait usecs between chunks\n");
    printf("  -x, --waitcsum <usec> - wait usecs before writing checksum\n");
    printf("  -l, --delayack - Delay before sending the last ack\n");
    printf("  -n, --nodelayack - No delay before sending the last ack\n");
    printf("  -f, --configdir <file> - Use the given directory for the radio"
	   " configuration instead\nof the default %s\n", RADIO_CONFIGDIR);
    printf("  -t, --prewritedelay <usec> - Delay added before every write\n");
}

int
main(int argc, char *argv[])
{
    int c;
    struct yaesu_data *d = NULL;
    struct yaesu_block *b;
    FILE *f;
    int rv;
    char dummy;
    char *dend;
    unsigned int i;

    char *gensiostr = "serialdev,/dev/ttyS0,9600n81";
    char *configdir = RADIO_CONFIGDIR;
    char *filename = "yaesu.rfile";
    int ignerr = 0;
    int do_read = 0;
    int do_write = 0;
    int send_echo = 0;
    int recv_echo = -1;
    int noendecho = -1;
    int do_checksum = -1;
    int do_checkblock = -1;
    int do_waitchecksum = -1;
    int waitchunk = -1;
    int csumdelay = -1;
    int chunksize = -1;
    int delayack = -1;
    int prewritedelay = -1;
    struct gensio *io;
    struct gensio_os_funcs *o;
    gensio_time zero_timeout = { 0, 0 }, timeout;
    gensiods count;

    progname = argv[0];

    while (1) {
	c = getopt_long(argc, argv, "?hva:b:irwt:emycgf:F:pqjkloux",
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

	    case 'f':
		filename = optarg;
		break;

	    case 'F':
		configdir = optarg;
		break;

	    case 'I':
		ignerr = 1;
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

	    case 'u':
		noendecho = 1;
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

	    case 'a':
		chunksize = strtoul_nooctal(optarg, &dend);
		if ((*optarg == '\0') || (*dend != '\0')) {
		    fprintf(stderr, "Invalid chunksize: '%s'.\n", optarg);
		    usage();
		    exit(1);
		}
		break;

	    case 'b':
		waitchunk = strtoul_nooctal(optarg, &dend);
		if ((*optarg == '\0') || (*dend != '\0')) {
		    fprintf(stderr, "Invalid waitchunk: '%s'.\n", optarg);
		    usage();
		    exit(1);
		}
		break;

	    case 'x':
		csumdelay = strtoul_nooctal(optarg, &dend);
		if ((*optarg == '\0') || (*dend != '\0')) {
		    fprintf(stderr, "Invalid csumdelay: '%s'.\n", optarg);
		    usage();
		    exit(1);
		}
		break;

	    case 't':
		prewritedelay = strtoul_nooctal(optarg, &dend);
		if ((*optarg == '\0') || (*dend != '\0')) {
		    fprintf(stderr, "Invalid prewritedelay: '%s'.\n", optarg);
		    usage();
		    exit(1);
		}
		break;

	    case 'l':
		delayack = 1;
		break;

	    case 'n':
		delayack = 0;
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
	fprintf(stderr, "Missing gensio string.\n");
	usage();
	exit(1);
    }
    gensiostr = argv[optind];

    read_yaesu_config(configdir);
 
    if ((do_read + do_write) > 1) {
	fprintf(stderr, "Can only specify one of -r and -w.\n");
	exit(1);
    }

    if (do_read) {
	f = fopen(filename, "wb");
	if (!f) {
	    fprintf(stderr, "Unable to open outfile %s\n", filename);
	    exit(1);
	}
    } else if (do_write) {
	f = fopen(filename, "rb");
	if (!f) {
	    fprintf(stderr, "Unable to open infile %s\n", filename);
	    exit(1);
	}
    } else {
	fprintf(stderr, "Must specify one of -r or -w.\n");
	exit(1);
    }

    rv = gensio_default_os_hnd(0, &o);
    if (rv) {
	fprintf(stderr, "Could not allocate OS handler: %s\n",
		gensio_err_to_str(rv));
	return 1;
    }
    gensio_os_funcs_set_vlog(o, do_vlog);

    rv = str_to_gensio(gensiostr, o, NULL, NULL, &io);
    if (rv) {
	fprintf(stderr, "Could not create gensio from %s: %s\n", gensiostr,
		gensio_err_to_str(rv));
	return 1;
    }

    d = alloc_yaesu_data(o, io, do_read, send_echo, recv_echo, noendecho,
			 do_checksum,
			 do_checkblock, do_waitchecksum, chunksize, waitchunk,
			 delayack, prewritedelay, csumdelay);
    if (!d) {
	fprintf(stderr, "Out of memory\n");
	exit(1);
    }
    gensio_set_callback(io, radio_event, d);

    rv = gensio_open_s(io);
    if (rv) {
	fprintf(stderr, "Could not open gensio %s: %s\n", gensiostr,
		gensio_err_to_str(rv));
	return 1;
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

    rv = gensio_set_sync(io);
    if (rv) {
	fprintf(stderr, "Could not set gensio sync for %s: %s\n", gensiostr,
		gensio_err_to_str(rv));
	return 1;
    }

    /* Flush the input buffer */
    do {
	rv = gensio_read_s(io, &count, &dummy, 1, &zero_timeout);
    } while (!rv && count > 0);
    if (rv) {
	fprintf(stderr, "Could not read from gensio %s: %s\n", gensiostr,
		gensio_err_to_str(rv));
	return 1;
    }

    if (d->recv_echo == -1) {
	/* Test for echo */
	rv = gensio_write_s(io, &count, "A", 1, NULL);
	if (rv || count != 1) {
	    fprintf(stderr, "Error testing echo, could not write to"
		    " gensio %s: %s\n", gensiostr, gensio_err_to_str(rv));
	    return 1;
	}

	timeout.secs = 0;
	timeout.nsecs = 200000000;
	rv = gensio_read_s(io, &count, &dummy, 1, &timeout);
	if (rv && rv != GE_TIMEDOUT) {
	    fprintf(stderr, "Could not read from gensio %s: %s\n", gensiostr,
		    gensio_err_to_str(rv));
	    return 1;
	}
	if (rv == GE_TIMEDOUT)
	    d->recv_echo = 0;
	else if (dummy == 'A')
	    d->recv_echo = 1;
	else {
	    fprintf(stderr, "Error testing echo, received character did"
		    " not match sent character.\n");
	    return 1;
	}
	if (verbose)
	    printf("Receive echo is %s\n", d->recv_echo ? "on" : "off");
    }

    rv = gensio_clear_sync(io);
    if (rv) {
	fprintf(stderr, "Could not clear gensio sync for %s: %s\n", gensiostr,
		gensio_err_to_str(rv));
	return 1;
    }

    gensio_set_read_callback_enable(io, true);

    if (do_write) {
	char c;
	printf("Put the radio in rx mode and press enter");
	fflush(stdout);
	fread(&c, 1, 1, stdin);
	rv = yaesu_start_write(d);
	if (rv) {
	    fprintf(stderr, "error starting write %s\n", gensio_err_to_str(rv));
	    goto out_err;
	}
    } else
	printf("Start transmission from the radio\n");

    rv = 0;
    while (!yaesu_is_done(d)) {
	yaesu_get_timeout(d, &timeout);
	rv = gensio_os_funcs_service(o, &timeout);
	if (rv == GE_TIMEDOUT) {
	    rv = handle_yaesu_timeout(d);
	    if (rv) {
		fprintf(stderr, "Error: %s\n", gensio_err_to_str(rv));
		goto out;
	    }
	} else if (rv) {
	    fprintf(stderr, "Service error: %s\n", gensio_err_to_str(rv));
	    goto out;
	}
    }

 out:
    if (hash || d->timeout_mode)
	printf("\n");
    printf("Transferred %d characters\n", d->data_count);

    /* FIXME - check d->err */
    if (!rv || ignerr) {
	b = d->head.next;
	while (b != &d->head) {
	    fwrite(b->buff, b->len, 1, f);
	    b = b->next;
	}
    }

 out_err:

    if (io) {
	gensio_close_s(io);
	gensio_free(io);
    }

    if (d)
	free_yaesu_data(d);

    return 0;
}
