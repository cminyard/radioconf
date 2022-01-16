/*
 *  kenwood_rw - Interface to Kenwood radio memory interfaces
 *  Copyright (C) 2022  Corey Minyard
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

/*
 * Read/write Kenwood TM-V71A, info from:
 *
 *    https://github.com/LA3QMA/TM-V71_TM-D710-Kenwood/
 *
 * on the details.  No support for the TM-D710, but that should be
 * easy to add.
 */

#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <gensio/gensio.h>

static unsigned int debug;
static const char *progname;

static void
do_vlog(struct gensio_os_funcs *f, enum gensio_log_levels level,
	const char *log, va_list args)
{
    fprintf(stderr, "gensio %s log: ", gensio_log_level_to_str(level));
    vfprintf(stderr, log, args);
    fprintf(stderr, "\n");
}

static void
help(int rv)
{
#define p printf
    p("A program for reading and writing memory data on a Kenwood radio\n");
    p("%s [-d|--debug] [-o|--outfile <file>] [-i|--infile <file>]\n", progname);
    p("   --read|--write  <gensio>\n");
    p("\n");
    p(" --debug - Increase the debugging level.\n");
    p(" --outfile - When reading, the file to store the data in.\n");
    p("             The default is kenwood.rfile\n");
    p(" --infile - When writing, where to get the data from.\n");
    p("            This must be specified with --write\n");
    p(" --read | --write - One of these must be specified.\n");
    p(" <gensio> - A gensio to connect to the radio using.\n");
    p("\n");
    p("The gensio depends on the radio and the baud rate.  If it's hookd to\n");
    p("/dev/ttyUSB0 and the baud rate was 9600, you would use:\n");
    p("  serialdev,/dev/ttyUSB0,9600n81\n");
    p("You can also make network connections and others.  See gensio.5 for\n");
    p("details.\n");
#undef p
    exit(rv);
}

static int
cmparg(int argc, char *argv[], int *arg, char *sarg, char *larg,
       const char **opt)
{
    char *a = argv[*arg];

    if ((sarg && strcmp(a, sarg) == 0) || (larg && strcmp(a, larg) == 0)) {
	if (!opt)
	    return 1;
	(*arg)++;
	if (*arg >= argc) {
	    fprintf(stderr, "No argument given for option %s\n", a);
	    return -1;
	}
	*opt = argv[*arg];
	return 1;
    } else if (larg && opt) {
	unsigned int len = strlen(larg);

	if (strncmp(a, larg, len) == 0 && a[len] == '=') {
	    *opt = a + len + 1;
	    return 1;
	}
    }

    return 0;
}

struct radio_info {
    struct gensio_os_funcs *o;
    struct gensio *io;
    int err;
    enum {
	  GET_ID,
	  ENABLE_PROGRAM,
	  GET_DATA_HEADER,
	  GET_DATA,
	  GET_DATA_ACK,
	  WRITE_DATA_HEADER,
	  WRITE_DATA,
	  WRITE_DATA_ACK,
	  LEAVE_PROGRAM,
	  RI_DONE
    } state;

    bool write;
    unsigned int memorylen;

    bool set_timeout;
    gensio_time timeout;

    char id[9];

    unsigned char cmddata[128];
    unsigned int cmdpos;
    unsigned int cmdlen;

    unsigned char rspdata[128];
    unsigned int rsppos;
    unsigned int rsplen;

    unsigned char readdata[0x8000];
    unsigned int readpos;
    unsigned int readlen;
    unsigned int readchunk_left;

    unsigned char writedata[0x8000];
    unsigned int writepos;
    unsigned int writelen;
    unsigned int writechunk_left;
};

struct radio {
    char *name;
    unsigned int memorylen;
} radios[] = {
    { "TM-V71", 0x7df0 },
    { NULL }
};

static struct radio *
find_radio(char *str)
{
    unsigned int i;

    for (i = 0; radios[i].name; i++) {
	if (strcmp(radios[i].name, str) == 0) {
	    return &(radios[i]);
	}
    }

    return NULL;
}

static void
read_next_data(struct radio_info *ri)
{
    unsigned int len;

    ri->cmddata[0] = 'R';
    ri->cmddata[1] = (ri->readpos >> 8) & 0xff;
    ri->cmddata[2] = ri->readpos & 0xff;
    len = ri->readlen - ri->readpos;
    if (len > 0xff)
	len = 0xff;
    ri->readchunk_left = len;
    ri->cmddata[3] = len;
    ri->cmdlen = 4;
    ri->cmdpos = 0;
    ri->rsplen = 4;
    ri->state = GET_DATA_HEADER;
    gensio_set_write_callback_enable(ri->io, true);
    if (debug >= 1)
	printf("Reading data chunk at 0x%4.4x\n", ri->readpos);
}

static void
write_next_data(struct radio_info *ri)
{
    unsigned int len;

    ri->cmddata[0] = 'W';
    ri->cmddata[1] = (ri->writepos >> 8) & 0xff;
    ri->cmddata[2] = ri->writepos & 0xff;
    len = ri->writelen - ri->writepos;
    if (len > 0xff)
	len = 0xff;
    ri->writechunk_left = len;
    ri->cmddata[3] = len;
    ri->cmdlen = 4;
    ri->cmdpos = 0;
    ri->state = WRITE_DATA_HEADER;
    gensio_set_write_callback_enable(ri->io, true);
    gensio_set_read_callback_enable(ri->io, false);
    if (debug >= 1)
	printf("Writing data chunk at 0x%4.4x\n", ri->writepos);
}

static void
handle_radio_rsp(struct radio_info *ri, char *rsp)
{
    struct radio *r;
    unsigned int len;

    switch (ri->state) {
    case GET_ID:
	if (debug >= 1)
	    printf("Radio id is: %s\n", rsp);
	len = strlen(rsp + 3);
	if (len < 4) {
	    fprintf(stderr, "Invalid data fetching id: %s\n", rsp);
	    ri->err = GE_INVAL;
	    return;
	}
	if (len > 8)
	    len = 8;
	/* + 3 to skip the incoming "ID " */
	memcpy(ri->id, rsp + 3, len);
	if (ri->write && strcmp(rsp + 3, ri->id) != 0) {
	    fprintf(stderr,
		    "Radio mismatch between file and radio, "
		    "expected '%s', got '%s' from radio",
		    ri->id, rsp);
	    ri->err = GE_INVAL;
	    return;
	}
	r = find_radio(rsp + 3);
	if (!r) {
	    fprintf(stderr, "Unknown radio type: %s\n", rsp);
	    ri->err = GE_INVAL;
	    return;
	}
	ri->memorylen = r->memorylen;
	ri->state = ENABLE_PROGRAM;
	strncpy((char *) ri->cmddata, "0M PROGRAM\r",
		sizeof(ri->cmddata));
	ri->cmdlen = strlen((char *) ri->cmddata);
	gensio_set_write_callback_enable(ri->io, true);
	break;

    case ENABLE_PROGRAM:
	if (debug >= 1)
	    printf("In program mode\n");
	if (strcmp(rsp, "0M") != 0) {
	    fprintf(stderr, "Error response enabling programming: %s\n", rsp);
	    ri->err = GE_INVAL;
	    return;
	}
	if (ri->write) {
	    ri->writelen = ri->memorylen;
	    write_next_data(ri);
	} else {
	    ri->readlen = ri->memorylen;
	    read_next_data(ri);
	}
	break;

    default:
	assert(0);
	break;
    }
}

static void
handle_radio_data(struct radio_info *ri, unsigned char *data, unsigned int len)
{
    switch (ri->state) {
    case GET_DATA_HEADER:
	ri->cmddata[0] = 'W';
	if (memcmp(data, ri->cmddata, 4) != 0) {
	    fprintf(stderr, "Unknown response from reading radio data: "
		    "%2.2x %2.2x %2.2x %2.2x\n",
		    data[0], data[1], data[2], data[3]);
	    ri->err = GE_INVAL;
	    return;
	}
	ri->state = GET_DATA;
	break;

    case GET_DATA:
	ri->cmddata[0] = 0x06;
	ri->cmdlen = 1;
	ri->state = GET_DATA_ACK;
	ri->rsplen = 1;
	gensio_set_write_callback_enable(ri->io, true);
	break;

    case GET_DATA_ACK:
	if (data[0] != 0x06) {
	    fprintf(stderr,
		    "Error response from reading radio data: %2.2x", data[0]);
	    ri->err = GE_INVAL;
	    return;
	}
	if (ri->readpos == ri->readlen) {
	    if (debug >= 1)
		printf("Done reading programming data\n");
	    ri->readpos = 0;
	    ri->cmddata[0] = 'E';
	    ri->cmdlen = 1;
	    ri->rsplen = 3;
	    ri->state = LEAVE_PROGRAM;
	    gensio_set_write_callback_enable(ri->io, true);
	} else {
	    read_next_data(ri);
	}
	break;

    case WRITE_DATA_ACK:
	if (data[0] != 0x06) {
	    fprintf(stderr,
		    "Error response from writing radio data: %2.2x", data[0]);
	    ri->err = GE_INVAL;
	    return;
	}
	if (ri->writepos == ri->writelen) {
	    if (debug >= 1)
		printf("Done writing programming data\n");
	    ri->writepos = 0;
	    ri->cmddata[0] = 'E';
	    ri->cmdlen = 1;
	    ri->rsplen = 3;
	    ri->state = LEAVE_PROGRAM;
	    gensio_set_write_callback_enable(ri->io, true);
	} else {
	    write_next_data(ri);
	}
	break;

    case LEAVE_PROGRAM:
	if (debug >= 1)
	    printf("Left programming mode\n");
	if (data[0] != 0x06) {
	    fprintf(stderr,
		    "Error response(2) from reading radio data: %2.2x",
		    data[0]);
	    ri->err = GE_INVAL;
	    return;
	}
	if (data[1] != 0x0d || data[2] != 0) {
	    fprintf(stderr, "Error response leaving program mode: %2.2x %2.2x",
		    data[1], data[2]);
	    ri->err = GE_INVAL;
	    return;
	}
	ri->state = RI_DONE;
	break;

    default:
	assert(0);
    }
}

static void
handle_radio_read(struct radio_info *ri, unsigned char *buf, gensiods *buflen)
{
    gensiods i;

    for (i = 0; !ri->err && i < *buflen; i++) {
	switch (ri->state) {
	case GET_DATA:
	    ri->readdata[ri->readpos++] = buf[i];
	    ri->readchunk_left--;
	    if (ri->readchunk_left == 0)
		handle_radio_data(ri, ri->readdata, ri->readlen);
	    break;

	case GET_DATA_HEADER:
	case GET_DATA_ACK:
	case LEAVE_PROGRAM:
	case WRITE_DATA_ACK:
	    ri->rspdata[ri->rsppos++] = buf[i];
	    if (ri->rsppos == ri->rsplen) {
		ri->rsppos = 0;
		handle_radio_data(ri, ri->rspdata, ri->rsplen);
	    }
	    break;

	default:
	    if (ri->rsppos >= sizeof(ri->rspdata)) {
		fprintf(stderr, "Response from radio was too long\n");
		ri->err = GE_INVAL;
		return;
	    }
	    if (buf[i] == '\r') {
		ri->rspdata[ri->rsppos++] = '\0';
		ri->rsppos = 0;
		handle_radio_rsp(ri, (char *) ri->rspdata);
	    } else {
		ri->rspdata[ri->rsppos++] = buf[i];
	    }
	}
    }
}

static void
handle_radio_write(struct radio_info *ri)
{
    gensiods count;
    int rv;

    if (ri->state != WRITE_DATA) {
	if (ri->cmdpos < ri->cmdlen) {
	    if (debug >= 2)
		printf("Writing command\n");
	    rv = gensio_write(ri->io, &count, ri->cmddata + ri->cmdpos,
			      ri->cmdlen - ri->cmdpos, NULL);
	    if (rv) {
		fprintf(stderr, "Error writing data: %s\n",
			gensio_err_to_str(rv));
		ri->err = rv;
		return;
	    }
	    ri->cmdpos += count;
	    if (ri->cmdpos == ri->cmdlen) {
		if (debug >= 2)
		    printf("Command done\n");
		ri->cmdpos = 0;
		ri->cmdlen = 0;
		if (ri->state == WRITE_DATA_HEADER)
		    ri->state = WRITE_DATA;
		else
		    gensio_set_write_callback_enable(ri->io, false);
	    }
	} else {
	    gensio_set_write_callback_enable(ri->io, false); /* Just in case */
	}
    }
    if (ri->state == WRITE_DATA) {
	rv = gensio_write(ri->io, &count, ri->writedata + ri->writepos,
			  ri->writechunk_left, NULL);
	if (rv) {
	    fprintf(stderr, "Error writing data: %s\n",
		    gensio_err_to_str(rv));
	    ri->err = rv;
	    return;
	}
	if (debug >= 2)
	    printf("Wrote %ld bytes\n", count);
	ri->writechunk_left -= count;
	ri->writepos += count;
	if (ri->writechunk_left == 0) {
	    if (debug >= 2)
		printf("Write done\n");
	    ri->state = WRITE_DATA_ACK;
	    ri->rsplen = 1;
	    ri->rsppos = 0;
	    gensio_set_read_callback_enable(ri->io, true);
	    gensio_set_write_callback_enable(ri->io, false);
	}
    }
}

static int
radio_event(struct gensio *io, void *user_data, int event, int err,
	    unsigned char *buf, gensiods *buflen,
	    const char *const *auxdata)
{
    struct radio_info *ri = user_data;

    if (err) {
	if (!ri->err)
	    ri->err = err;
	fprintf(stderr, "Read error: %s\n", gensio_err_to_str(err));
    }
    if (ri->err) {
	gensio_set_read_callback_enable(ri->io, false);
	gensio_set_write_callback_enable(ri->io, false);
	return 0;
    }

    switch (event) {
    case GENSIO_EVENT_READ:
	handle_radio_read(ri, buf, buflen);
	ri->set_timeout = true;
	ri->timeout.secs = 3;
	return 0;

    case GENSIO_EVENT_WRITE_READY:
	handle_radio_write(ri);
	ri->set_timeout = true;
	ri->timeout.secs = 3;
	return 0;

    default:
	return GE_NOTSUP;
    }
}

int
main(int argc, char *argv[])
{
    int i, rv, rv2;
    const char *outfile = "kenwood.rfile";
    const char *infile = NULL;
    struct radio_info ri;
    bool read = false, write = false;
    gensio_time timeout;
    const char *gensio;

    progname = argv[0];
    memset(&ri, 0, sizeof(ri));

    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
	if ((rv = cmparg(argc, argv, &i, "-d", "--debug", NULL))) {
	    debug++;
	} else if ((rv = cmparg(argc, argv, &i, "-o", "--outfile", &outfile))) {
	    /* Nothing to do. */
	} else if ((rv = cmparg(argc, argv, &i, "-i", "--infile", &infile))) {
	    /* Nothing to do. */
	} else if ((rv = cmparg(argc, argv, &i, "-r", "--read", NULL))) {
	    read = true;
	} else if ((rv = cmparg(argc, argv, &i, "-w", "--write", NULL))) {
	    write = true;
	} else {
	    fprintf(stderr, "Unknown argument: %s\n", argv[i]);
	    help(1);
	}
    }

    if ((!read && !write) || (read && write)) {
	fprintf(stderr, "Must specify only one of --read or --write\n");
	help(1);
    }
    if (write && !infile) {
	fprintf(stderr, "Must specify an infile with --write\n");
	help(1);
    }

    if (i >= argc) {
	fprintf(stderr, "No gensio string given to connect to\n");
	help(1);
    }

    gensio = argv[i];

    ri.write = write;
    if (write) {
	FILE *f = fopen(infile, "rb");
	struct radio *r;
	
	if (!f) {
	    fprintf(stderr, "Unable to open file %s\n", infile);
	    rv = GE_INVAL;
	} else {
	    i = fread(ri.id, 1, 8, f);
	    if ((unsigned int) i != 8) {
		printf("Error reading file %s\n", infile);
		return 1;
	    }
	    r = find_radio(ri.id);
	    if (!r) {
		fprintf(stderr, "Unknown radio type in file: %s\n", ri.id);
		return 1;
	    }
	    ri.memorylen = r->memorylen;
	    i = fread(ri.writedata, 1, ri.memorylen, f);
	    if ((unsigned int) i != ri.memorylen) {
		fprintf(stderr, "Error reading file %s\n", infile);
		return 1;
	    }
	    fclose(f);
	}
    }

    rv = gensio_default_os_hnd(0, &ri.o);
    if (rv) {
	fprintf(stderr, "Could not allocate OS handler: %s\n",
		gensio_err_to_str(rv));
	return 1;
    }
    gensio_os_funcs_set_vlog(ri.o, do_vlog);

    rv = str_to_gensio(gensio, ri.o, radio_event, &ri, &ri.io);
    if (rv) {
	fprintf(stderr, "Could not create gensio from %s: %s\n", gensio,
		gensio_err_to_str(rv));
	return 1;
    }

    rv = gensio_open_s(ri.io);
    if (rv) {
	fprintf(stderr, "Could not open gensio from %s: %s\n", gensio,
		gensio_err_to_str(rv));
	return 1;
    }

    if (debug >= 1)
	printf("Reading ID\n");
    strncpy((char *) ri.cmddata, "ID\r", sizeof(ri.cmddata));
    ri.cmdlen = 3;

    timeout.secs = 3;
    timeout.nsecs = 0;

    gensio_set_write_callback_enable(ri.io, true);
    gensio_set_read_callback_enable(ri.io, true);

    while (!ri.err && ri.state != RI_DONE) {
	rv = gensio_os_funcs_service(ri.o, &timeout);
	if (rv == GE_TIMEDOUT) {
	    fprintf(stderr, "Timed out waiting for radio\n");
	    break;
	}
	if (rv) {
	    fprintf(stderr, "Error waiting for radio: %s\n",
		    gensio_err_to_str(rv));
	    break;
	}
	if (ri.set_timeout) {
	    timeout = ri.timeout;
	    ri.set_timeout = false;
	}
    }

    rv2 = gensio_close_s(ri.io);
    if (rv2)
	fprintf(stderr, "Could not close gensio from %s: %s\n", gensio,
		gensio_err_to_str(rv));

    if (ri.io)
	gensio_free(ri.io);

    if (!rv && !ri.err && read) {
	FILE *f = fopen(outfile, "wb");

	if (!f) {
	    fprintf(stderr, "Unable to open file %s\n", outfile);
	    rv = GE_INVAL;
	} else {
	    i = fwrite(ri.id, 1, 8, f);
	    if ((unsigned int) i != 8) {
		fprintf(stderr, "Error writing to file %s\n", outfile);
		rv = GE_INVAL;
	    }
	    i = fwrite(ri.readdata, 1, ri.readlen, f);
	    if ((unsigned int) i != ri.readlen) {
		fprintf(stderr, "Error writing to file %s\n", outfile);
		rv = GE_INVAL;
	    }
	    fclose(f);
	    printf("Wrote %s data to %s.\n", ri.id, outfile);
	}
    }

    if (rv || rv2 || ri.err)
	return 1;
    return 0;
}
