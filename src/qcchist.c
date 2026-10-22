/**********************************************************
 * qcc session-history view.
 *
 * Compiled only with --enable-qcc-binlog. The daemon is not
 * involved: qcc opens the binlog path from -B and paints this
 * screen itself. The wire protocol does not grow a command.
 *
 * The view is today's local calendar day, walked backward from
 * the end of the file so the older history is never read. While
 * the view is up, a once-a-second stat appends frames that
 * landed after the offset we already accepted. A short tail
 * fails the CRC check and is left for the next tick. This file
 * does not lock the log and does not truncate it.
 **********************************************************/

#include <config.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifdef HAVE_NCURSES_H
#include <ncurses.h>
#else
#include <curses.h>
#endif

#include "types.h"
#include "qslib.h"
#include "binlogfmt.h"
#include "qcchist.h"

/* Newest sessions kept for one local day. Older ones in that day are dropped. */
#define HIST_MAX	4096

static char *path;
static bl_sess_t *recs;
static int nrecs;
static int cursor;
static int top;
static off_t file_end;
static int ready;
static int on;
static int day_yday;
static int day_year;
static int list_mh;
static WINDOW *whist;
static char title_buf[80];
static char status_buf[BL_TEXT_LEN + 8];
static char errbuf[160];
/* Address column. At least the header "Address", otherwise the longest loaded one. */
static int addr_col = 7;


static int read_full(int fd, void *buf, size_t len)
{
	unsigned char *p = (unsigned char *) buf;

	while ( len ) {
		ssize_t n = read( fd, p, len );

		if ( n < 0 ) {
			if ( errno == EINTR )
				continue;
			return -1;
		}
		if ( n == 0 )
			return -1;
		p += n;
		len -= (size_t) n;
	}
	return 0;
}


/*
 * One decimal place only while the coefficient is a single digit,
 * so the text is never longer than four characters: "1.5k", "10k",
 * "100k", "1.0M", "4.0G". A coefficient that would show as 1000 or
 * more is the next unit instead. 1024 bytes is "1.0k", 1 MiB is
 * "1.0M", 1 GiB is "1.0G". Plain bytes stop at 999.
 */
static void format_bytes(unsigned long long v, char *dst, size_t n)
{
	const char *suf = "k";
	unsigned long long div = 1024ull;
	int hops;

	if ( v < 1000ull ) {
		snprintf( dst, n, "%lu", (unsigned long) v );
		return;
	}
	for ( hops = 0; hops < 3; hops++ ) {
		unsigned long long num = v / div;
		unsigned long long rem = v % div;
		unsigned long long tenths = ( rem * 10ull + div / 2ull ) / div;
		unsigned long long whole = num;

		if ( tenths >= 10ull ) {
			whole++;
			tenths -= 10ull;
		}
		/*
		 * 1024 of this unit is the next unit. hops 0 is k, 1 is M.
		 * G is the last step the 32-bit session total can reach.
		 */
		if ( whole >= 1000ull && hops < 2 ) {
			div *= 1024ull;
			suf = hops ? "G" : "M";
			continue;
		}
		if ( whole < 10ull ) {
			snprintf( dst, n, "%lu.%lu%s",
				(unsigned long) whole, (unsigned long) tenths, suf );
		} else {
			if ( tenths >= 5ull )
				whole++;
			if ( whole >= 1000ull && hops < 2 ) {
				div *= 1024ull;
				suf = hops ? "G" : "M";
				continue;
			}
			snprintf( dst, n, "%lu%s", (unsigned long) whole, suf );
		}
		return;
	}
	snprintf( dst, n, "%luG",
		(unsigned long) ( v / ( 1024ull * 1024ull * 1024ull )));
}


/* Right-align src into exactly width columns. A longer src keeps its tail. */
static void rfield(char *dst, int width, const char *src)
{
	int n, pad, i;

	if ( width < 0 )
		width = 0;
	n = src ? (int) strlen( src ) : 0;
	if ( n > width ) {
		src += n - width;
		n = width;
	}
	pad = width - n;
	for ( i = 0; i < pad; i++ )
		dst[i] = ' ';
	if ( n && src )
		memcpy( dst + pad, src, (size_t) n );
	dst[width] = '\0';
}


static void layout_for(int col, int *gap1, int *gap2, int *addr_w, int *info_w)
{
	int extra = 0;
	int fixed;

	*gap1 = 1;
	*gap2 = 1;
	*addr_w = addr_col;
	if ( col > 78 )
		extra = col - 78;
	if ( extra > 0 ) {
		*gap1 = 2;
		extra--;
	}
	if ( extra > 0 )
		*gap2 = 2;
	fixed = 11 + *gap1 + 4 + *gap2 + *addr_w + 2 + 9 + 2;
	*info_w = col - fixed;
	if ( *info_w < 0 )
		*info_w = 0;
}


/* Full address, or Unresolved when every 16-bit field is zero. */
static void full_addr(char *dst, size_t n, const bl_sess_t *s)
{
	if ( !s->zone && !s->net && !s->node && !s->point )
		snprintf( dst, n, "Unresolved" );
	else if ( s->point )
		snprintf( dst, n, "%u:%u/%u.%u", s->zone, s->net, s->node, s->point );
	else
		snprintf( dst, n, "%u:%u/%u", s->zone, s->net, s->node );
}


/* Longest address currently in the buffer. The column grows when a longer one arrives. */
static void measure_addrs(void)
{
	int i, n, best = 7;
	char buf[64];

	for ( i = 0; i < nrecs; i++ ) {
		full_addr( buf, sizeof( buf ), &recs[i] );
		n = (int) strlen( buf );
		if ( n > best )
			best = n;
	}
	addr_col = best;
}


/*
 * Address column. Wider than the field: drop the zone and the colon.
 * Still wider: cut the right side. The result is exactly width columns,
 * padded with spaces when the text is shorter.
 */
static void fit_addr(char *dst, int width, const bl_sess_t *s)
{
	char full[64];
	const char *p;
	int n, i;

	if ( width < 0 )
		width = 0;
	full_addr( full, sizeof( full ), s );
	p = full;
	if ( (int) strlen( p ) > width ) {
		const char *colon = strchr( p, ':' );

		if ( colon && colon[1] )
			p = colon + 1;
	}
	n = (int) strlen( p );
	if ( n > width )
		n = width;
	memcpy( dst, p, (size_t) n );
	for ( i = n; i < width; i++ )
		dst[i] = ' ';
	dst[width] = '\0';
}


static void put_span(char *dst, int *used, int max, const char *src, int n)
{
	int i;

	if ( n < 0 )
		n = src ? (int) strlen( src ) : 0;
	for ( i = 0; i < n && *used < max; i++ )
		dst[(*used)++] = src ? src[i] : ' ';
}


static void put_spaces(char *dst, int *used, int max, int n)
{
	while ( n-- > 0 && *used < max )
		dst[(*used)++] = ' ';
}


/*
 * T-Hist text: present fields joined by "; ".
 * A failed session puts the reason first, then peer, system, location, sysop.
 */
static void fill_info(char *dst, size_t n, const bl_sess_t *s)
{
	const char *part[5];
	int i, k = 0, used = 0;

	if ( !n )
		return;
	dst[0] = '\0';
	if ( !( s->status & BL_OK ) && s->result[0] )
		part[k++] = s->result;
	if ( s->peer[0] )
		part[k++] = s->peer;
	if ( s->sysname[0] )
		part[k++] = s->sysname;
	if ( s->location[0] )
		part[k++] = s->location;
	if ( s->sysop[0] )
		part[k++] = s->sysop;
	for ( i = 0; i < k; i++ ) {
		const char *p = part[i];

		if ( i && used + 2 < (int) n ) {
			dst[used++] = ';';
			dst[used++] = ' ';
		}
		while ( *p && used + 1 < (int) n )
			dst[used++] = *p++;
	}
	dst[used] = '\0';
}


/* One list row, or the header when rec is NULL. */
static void build_line(char *dst, int max, int col, const bl_sess_t *s)
{
	int gap1, gap2, addr_w, info_w, used = 0;
	char addr[32], traffic[16], info[1100], sent[16], rcvd[16];
	char tbuf[16], flags[5];

	if ( max < 1 ) {
		if ( max == 0 && dst )
			dst[0] = '\0';
		return;
	}
	layout_for( col, &gap1, &gap2, &addr_w, &info_w );
	if ( !s ) {
		/* Left-aligned over the 11-column time. The label is one shorter. */
		memset( tbuf, ' ', 11 );
		memcpy( tbuf, "Start Time", 10 );
		tbuf[11] = '\0';
		memcpy( flags, "DPLA", 4 );
		flags[4] = '\0';
		memset( addr, ' ', (size_t) addr_w );
		memcpy( addr, "Address", addr_w >= 7 ? 7 : addr_w );
		addr[addr_w] = '\0';
		{
			char a[8], b[8];

			rfield( a, 4, "Rcv" );
			rfield( b, 4, "Snt" );
			snprintf( traffic, sizeof( traffic ), "%s %s", a, b );
		}
		snprintf( info, sizeof( info ), "Info" );
	} else {
		time_t started = (time_t) s->started;
		struct tm *tm = localtime( &started );

		/* DD/MM HH:MM is 11 columns, the same width as the header field. */
		if ( tm )
			snprintf( tbuf, sizeof( tbuf ), "%02d/%02d %02d:%02d",
				tm->tm_mday, tm->tm_mon + 1, tm->tm_hour, tm->tm_min );
		else
			snprintf( tbuf, sizeof( tbuf ), "--/-- --:--" );
		flags[0] = ( s->status & BL_IN ) ? 'I' : 'O';
		flags[1] = ( s->status & BL_PWD ) ? 'P' : ' ';
		flags[2] = ( s->status & BL_LST ) ? 'L' : ' ';
		flags[3] = ( s->status & BL_OK ) ? ' ' : 'A';
		flags[4] = '\0';
		fit_addr( addr, addr_w, s );
		format_bytes( s->bytes_sent, sent, sizeof( sent ));
		format_bytes( s->bytes_rcvd, rcvd, sizeof( rcvd ));
		{
			char a[8], b[8];

			rfield( a, 4, rcvd );
			rfield( b, 4, sent );
			snprintf( traffic, sizeof( traffic ), "%s %s", a, b );
		}
		fill_info( info, sizeof( info ), s );
	}

	put_span( dst, &used, max, tbuf, 11 );
	put_spaces( dst, &used, max, gap1 );
	put_span( dst, &used, max, flags, 4 );
	put_spaces( dst, &used, max, gap2 );
	put_span( dst, &used, max, addr, addr_w );
	put_spaces( dst, &used, max, 2 );
	put_span( dst, &used, max, traffic, 9 );
	put_spaces( dst, &used, max, 2 );
	/*
	 * Copy the phrase only through its NUL. The spaces that follow
	 * are row padding so the cursor color reaches the right edge;
	 * they are not part of the phrase.
	 */
	{
		int i, cut = 0;

		for ( i = 0; i < info_w && info[i] && used < max; i++ )
			dst[used++] = info[i];
		if ( info[i] || used > col )
			cut = 1;
		if ( s && used < col ) {
			int pad_to = col < max - 1 ? col : max - 1;

			while ( used < pad_to )
				dst[used++] = ' ';
		}
		if ( cut && col > 0 && col <= used && col < max )
			dst[col - 1] = '.';
	}
	if ( used < max )
		dst[used] = '\0';
	else if ( max > 0 )
		dst[max - 1] = '\0';
}


static void fill_status(void)
{
	unsigned in = 0, out = 0, failed = 0;
	unsigned long long rcv = 0, snt = 0;
	char traffic[16], rcvd[16], sent[16];
	int i;

	for ( i = 0; i < nrecs; i++ ) {
		if ( recs[i].status & BL_IN )
			in++;
		if ( recs[i].status & BL_OUT )
			out++;
		if ( !( recs[i].status & BL_OK ))
			failed++;
		rcv += recs[i].bytes_rcvd;
		snt += recs[i].bytes_sent;
	}
	format_bytes( rcv + snt, traffic, sizeof( traffic ));
	format_bytes( rcv, rcvd, sizeof( rcvd ));
	format_bytes( snt, sent, sizeof( sent ));
	snprintf( status_buf, sizeof( status_buf ),
		"Total: In/Out %u/%u, Failed %u, Traffic %s, Rcv/Snt %s/%s",
		in, out, failed, traffic, rcvd, sent );
}


static void sync_caption(void)
{
	const bl_sess_t *s;

	title_buf[0] = '\0';
	fill_status();
	if ( nrecs <= 0 || cursor < 0 || cursor >= nrecs )
		return;
	s = &recs[cursor];
	full_addr( title_buf, sizeof( title_buf ), s );
}


static time_t local_midnight(time_t now, int *yday, int *year)
{
	struct tm copy;
	struct tm *p = localtime( &now );

	if ( !p )
		return now;
	copy = *p;
	if ( yday )
		*yday = copy.tm_yday;
	if ( year )
		*year = copy.tm_year;
	copy.tm_hour = 0;
	copy.tm_min = 0;
	copy.tm_sec = 0;
	copy.tm_isdst = -1;
	return mktime( &copy );
}


/*
 * A frame whose last byte is at end-1. 1 and *start is its first byte.
 * 0 means the trailing length byte does not describe a whole frame.
 * -1 is a read error. The image is the trimmed payload, not the wrapper.
 */
static int frame_ending_at(int fd, off_t end, off_t *start,
		unsigned char *image, unsigned *ilen)
{
	unsigned char b[1 + 255 + BL_FRAME_WRAP];
	unsigned char L;
	unsigned adv = 0;
	off_t pos;

	if ( end < (off_t) BL_SIG_LEN + 1 + BL_FRAME_WRAP )
		return 0;
	if ( lseek( fd, end - 1, SEEK_SET ) < 0 || read_full( fd, &L, 1 ) < 0 )
		return -1;
	if ( L < 1 )
		return 0;
	pos = end - (off_t) ( L + BL_FRAME_WRAP );
	if ( pos < (off_t) BL_SIG_LEN )
		return 0;
	if ( lseek( fd, pos, SEEK_SET ) < 0
		|| read_full( fd, b, (size_t) L + BL_FRAME_WRAP ) < 0 )
		return -1;
	if ( bl_frame_valid( b, (size_t) L + BL_FRAME_WRAP, &adv ) != 1 )
		return 0;
	if ( image ) {
		memset( image, 0, 256 );
		memcpy( image, b + 1, L );
	}
	if ( ilen )
		*ilen = L;
	if ( start )
		*start = pos;
	return 1;
}


/*
 * Exclusive end offset of the last complete frame. A writer in the
 * middle of an append leaves a short tail; that tail is not a frame,
 * so look back at most one maximum frame and do not shorten the file.
 */
static off_t good_tail(int fd, off_t end)
{
	unsigned char b[1 + 255 + BL_FRAME_WRAP];
	off_t from, pos, best;

	if ( frame_ending_at( fd, end, NULL, NULL, NULL ) == 1 )
		return end;
	from = end - (off_t) ( 255 + BL_FRAME_WRAP );
	if ( from < (off_t) BL_SIG_LEN )
		from = BL_SIG_LEN;
	best = from;
	pos = from;
	while ( pos < end && ( end - pos ) >= (off_t) ( 1 + BL_FRAME_WRAP )) {
		size_t n = (size_t) ( end - pos );
		unsigned adv = 0;

		if ( n > sizeof( b ))
			n = sizeof( b );
		if ( lseek( fd, pos, SEEK_SET ) < 0 || read_full( fd, b, n ) < 0 )
			return best;
		if ( bl_frame_valid( b, n, &adv ) == 1 && pos + (off_t) adv <= end ) {
			if ( pos + (off_t) adv > best )
				best = pos + (off_t) adv;
			pos += (off_t) adv;
			continue;
		}
		pos++;
	}
	return best;
}


static int check_sig(int fd)
{
	unsigned char sig[BL_SIG_LEN];

	if ( lseek( fd, 0, SEEK_SET ) < 0 || read_full( fd, sig, BL_SIG_LEN ) < 0 )
		return -1;
	if ( memcmp( sig, bl_sig, BL_SIG_LEN ) != 0 )
		return -1;
	return 0;
}


static void reverse_recs(bl_sess_t *a, int n)
{
	int i;

	for ( i = 0; i < n / 2; i++ ) {
		bl_sess_t tmp = a[i];

		a[i] = a[n - 1 - i];
		a[n - 1 - i] = tmp;
	}
}


/*
 * Walk backward from `end` while frames start at or after `stop` and
 * the session started at or after midnight. Newest frame is stored
 * first; the caller reverses. *end_out is the exclusive end we trust.
 */
static int collect_back(int fd, off_t end, off_t stop, time_t midnight,
		bl_sess_t **out, int *nout)
{
	off_t pos, start;
	unsigned char image[256];
	unsigned ilen = 0;
	int n = 0;

	*out = NULL;
	*nout = 0;
	pos = good_tail( fd, end );
	while ( n < HIST_MAX && pos > stop ) {
		bl_sess_t *grown;
		bl_sess_t one;

		if ( frame_ending_at( fd, pos, &start, image, &ilen ) != 1 )
			break;
		/* This frame was already accepted on a previous read. */
		if ( start < stop )
			break;
		bl_unpack( image, ilen, &one );
		if ( (time_t) one.started < midnight )
			break;
		grown = (bl_sess_t *) realloc( *out, (size_t) ( n + 1 ) * sizeof( bl_sess_t ));
		if ( !grown )
			return -1;
		*out = grown;
		(*out)[n++] = one;
		pos = start;
	}
	*nout = n;
	return 0;
}


static int open_log(int *fd, off_t *size)
{
	struct stat st;

	*fd = open( path, O_RDONLY );
	if ( *fd < 0 ) {
		snprintf( errbuf, sizeof( errbuf ), "can't open '%s': %s", path, strerror( errno ));
		return -1;
	}
	if ( fstat( *fd, &st ) < 0 ) {
		snprintf( errbuf, sizeof( errbuf ), "can't stat '%s': %s", path, strerror( errno ));
		close( *fd );
		return -1;
	}
	*size = st.st_size;
	if ( *size == 0 ) {
		close( *fd );
		*fd = -1;
		return 0;
	}
	if ( *size < (off_t) BL_SIG_LEN || check_sig( *fd ) < 0 ) {
		snprintf( errbuf, sizeof( errbuf ), "'%s' is not a T-Hist v3 log", path );
		close( *fd );
		return -1;
	}
	return 0;
}


static void adopt(bl_sess_t *fresh, int n, off_t end, int follow)
{
	free( recs );
	recs = fresh;
	nrecs = n;
	file_end = end;
	ready = 1;
	if ( nrecs <= 0 ) {
		cursor = 0;
		top = 0;
	} else if ( follow || cursor >= nrecs )
		cursor = nrecs - 1;
	if ( cursor < 0 )
		cursor = 0;
	measure_addrs();
	sync_caption();
}


static int load_day(time_t now)
{
	int fd, yday, year, n = 0;
	off_t size = 0;
	time_t midnight;
	bl_sess_t *fresh = NULL;

	midnight = local_midnight( now, &yday, &year );
	if ( open_log( &fd, &size ) < 0 )
		return -1;
	day_yday = yday;
	day_year = year;
	if ( fd < 0 ) {
		adopt( NULL, 0, 0, 1 );
		return 0;
	}
	if ( collect_back( fd, size, (off_t) BL_SIG_LEN, midnight, &fresh, &n ) < 0 ) {
		free( fresh );
		close( fd );
		snprintf( errbuf, sizeof( errbuf ), "out of memory reading '%s'", path );
		return -1;
	}
	reverse_recs( fresh, n );
	adopt( fresh, n, good_tail( fd, size ), 1 );
	close( fd );
	return 0;
}


static int catch_up(time_t now)
{
	int fd, yday, year, n = 0, follow, i;
	off_t size = 0, tail;
	time_t midnight;
	bl_sess_t *fresh = NULL;

	midnight = local_midnight( now, &yday, &year );
	if ( yday != day_yday || year != day_year ) {
		if ( load_day( now ) < 0 )
			return ready ? 0 : -1;
		return 1;
	}
	if ( open_log( &fd, &size ) < 0 )
		return ready ? 0 : -1;
	/*
	 * Nothing appended. size 0 has no descriptor: open_log closed it.
	 * A smaller file is a rotation or a writer repair, so the day
	 * is read again. An empty file after frames we already showed
	 * is that same case (file_end was past 0).
	 */
	if ( size == file_end ) {
		if ( fd >= 0 )
			close( fd );
		return 0;
	}
	if ( fd < 0 || size < file_end ) {
		if ( fd >= 0 )
			close( fd );
		if ( load_day( now ) < 0 )
			return -1;
		return 1;
	}
	tail = good_tail( fd, size );
	if ( tail <= file_end ) {
		close( fd );
		return 0;
	}
	if ( collect_back( fd, size, file_end, midnight, &fresh, &n ) < 0 ) {
		free( fresh );
		close( fd );
		return 0;
	}
	close( fd );
	reverse_recs( fresh, n );
	follow = ( nrecs == 0 ) || ( cursor == nrecs - 1 );
	for ( i = 0; i < n; i++ ) {
		bl_sess_t *grown;

		while ( nrecs >= HIST_MAX ) {
			memmove( recs, recs + 1, (size_t) ( nrecs - 1 ) * sizeof( *recs ));
			nrecs--;
			if ( cursor > 0 )
				cursor--;
			if ( top > 0 )
				top--;
		}
		grown = (bl_sess_t *) realloc( recs, (size_t) ( nrecs + 1 ) * sizeof( *recs ));
		if ( !grown ) {
			free( fresh );
			measure_addrs();
			sync_caption();
			return 0;
		}
		recs = grown;
		recs[nrecs++] = fresh[i];
	}
	free( fresh );
	file_end = tail;
	if ( follow && nrecs > 0 )
		cursor = nrecs - 1;
	measure_addrs();
	sync_caption();
	return n > 0;
}


static int list_height(void)
{
	int h = LINES - list_mh - 4;

	if ( h < 1 )
		h = 1;
	return h;
}


static void ensure_window(void)
{
	int h, w;

	if ( whist )
		return;
	h = list_height();
	w = COLS - 2;
	if ( w < 1 )
		w = 1;
	whist = newwin( h, w, list_mh + 2, 1 );
	if ( !whist )
		return;
	wbkgd( whist, COLOR_PAIR( 7 ) | ' ' );
	scrollok( whist, 0 );
}


static void draw_half(WINDOW *w, int y, int x, int width, const char *text, int attr)
{
	int h, ww;

	getmaxyx( w, h, ww );
	(void) ww;
	if ( y < 0 || y >= h || width <= 0 )
		return;
	wattrset( w, attr );
	mvwaddnstr( w, y, x, text, width );
}


void hist_set_path(const char *p)
{
	xfree( path );
	path = NULL;
	if ( p && *p )
		path = xstrdup( p );
}


int hist_on(void)
{
	return on;
}


const char *hist_title(void)
{
	return title_buf;
}


const char *hist_status(void)
{
	return status_buf;
}


const char *hist_toggle(int mh)
{
	list_mh = mh;
	if ( on ) {
		on = 0;
		if ( whist ) {
			delwin( whist );
			whist = NULL;
		}
		return NULL;
	}
	if ( !path || !*path )
		return "session history needs -B path";
	if ( !ready ) {
		if ( load_day( time( NULL )) < 0 )
			return errbuf;
	} else if ( catch_up( time( NULL )) < 0 && !ready )
		return errbuf;
	on = 1;
	ensure_window();
	sync_caption();
	return NULL;
}


void hist_resize(int mh)
{
	int h, w;

	list_mh = mh;
	if ( !whist )
		return;
	h = list_height();
	w = COLS - 2;
	if ( w < 1 )
		w = 1;
	wresize( whist, h, w );
	mvwin( whist, mh + 2, 1 );
}


int hist_poll(time_t now)
{
	if ( !on || !path )
		return 0;
	return catch_up( now ) > 0;
}


void hist_key(int ch, int body)
{
	int page;

	if ( body < 1 )
		body = 1;
	page = body > 1 ? body - 1 : 1;
	if ( nrecs <= 0 ) {
		cursor = 0;
		top = 0;
		return;
	}
	switch ( ch ) {
	case KEY_UP:
		if ( cursor > 0 )
			cursor--;
		break;
	case KEY_DOWN:
		if ( cursor + 1 < nrecs )
			cursor++;
		break;
	case KEY_PPAGE:
		cursor -= page;
		if ( cursor < 0 )
			cursor = 0;
		break;
	case KEY_NPAGE:
		cursor += page;
		if ( cursor >= nrecs )
			cursor = nrecs - 1;
		break;
	default:
		break;
	}
	sync_caption();
}


static void place_cursor(int body)
{
	if ( body < 1 )
		body = 1;
	if ( nrecs <= 0 ) {
		cursor = 0;
		top = 0;
		return;
	}
	if ( cursor < 0 )
		cursor = 0;
	if ( cursor >= nrecs )
		cursor = nrecs - 1;
	if ( top > cursor )
		top = cursor;
	if ( cursor >= top + body )
		top = cursor - body + 1;
	if ( top < 0 )
		top = 0;
}


void hist_hide_list(void)
{
	if ( !whist )
		return;
	delwin( whist );
	whist = NULL;
}


void hist_draw_list(void)
{
	int h, w, body, row, y;
	char line[2048];

	ensure_window();
	if ( !whist )
		return;
	getmaxyx( whist, h, w );
	werase( whist );
	if ( w > (int) sizeof( line ) - 1 )
		w = (int) sizeof( line ) - 1;
	build_line( line, (int) sizeof( line ), w, NULL );
	wattrset( whist, COLOR_PAIR( 6 ) | A_BOLD );
	mvwaddnstr( whist, 0, 0, line, w );
	body = h - 1;
	place_cursor( body );
	if ( nrecs <= 0 ) {
		wattrset( whist, COLOR_PAIR( 7 ));
		mvwaddnstr( whist, h > 1 ? 1 : 0, 0, "No sessions today", w );
		wrefresh( whist );
		return;
	}
	{
		int gap1, gap2, addr_w, info_w, ax, addr_x;

		layout_for( w, &gap1, &gap2, &addr_w, &info_w );
		(void) info_w;
		ax = 11 + gap1 + 3;
		addr_x = 11 + gap1 + 4 + gap2;
		for ( row = 0, y = 1; y < h && top + row < nrecs; y++, row++ ) {
			int cur = ( top + row == cursor );
			const bl_sess_t *s = &recs[top + row];

			build_line( line, (int) sizeof( line ), w, s );
			wattrset( whist, cur ? COLOR_PAIR( 16 ) : COLOR_PAIR( 7 ));
			mvwaddnstr( whist, y, 0, line, w );
			if ( ax < w && line[ax] == 'A' ) {
				wattrset( whist, COLOR_PAIR( 4 ));
				mvwaddch( whist, y, ax, 'A' );
			}
			if ( !cur && addr_x < w && addr_w > 0 ) {
				int n = addr_w;

				if ( addr_x + n > w )
					n = w - addr_x;
				wattrset( whist, COLOR_PAIR( 3 ));
				mvwaddnstr( whist, y, addr_x, line + addr_x, n );
			}
		}
	}
	wrefresh( whist );
}


static void fmt_hms(char *dst, size_t n, unsigned sec)
{
	snprintf( dst, n, "%02u:%02u:%02u", sec / 3600, ( sec % 3600 ) / 60, sec % 60 );
}


/* Plain bytes keep the word. k/M/G stay the four-character form. */
static void fmt_amount(char *dst, size_t n, unsigned long long v)
{
	if ( v < 1000ull ) {
		snprintf( dst, n, "%lu bytes", (unsigned long) v );
		return;
	}
	format_bytes( v, dst, n );
}


/* Left text from column 1. Right text ends on the last column. */
static void draw_lr(WINDOW *w, int y, int width,
		const char *left, int lattr, const char *right, int rattr)
{
	int ln = 0, rn = 0, room;

	if ( y < 0 || width < 1 )
		return;
	if ( right && right[0] )
		rn = (int) strlen( right );
	if ( rn > width )
		rn = width;
	if ( rn > 0 )
		draw_half( w, y, width - rn, rn, right, rattr );
	room = width - 1 - ( rn ? rn + 1 : 0 );
	if ( left && left[0] && room > 0 ) {
		ln = (int) strlen( left );
		if ( ln > room )
			ln = room;
		draw_half( w, y, 1, ln, left, lattr );
	}
}


void hist_draw_card(WINDOW *wmain)
{
	const bl_sess_t *s;
	int h, ww, edge, badges;
	char left[256], right[256], addr[64], when[40], dur[16];
	char rcvd[32], sent[32];
	time_t started;

	if ( !wmain )
		return;
	getmaxyx( wmain, h, ww );
	edge = ww > 0 ? ww - 1 : 0;
	werase( wmain );
	if ( nrecs <= 0 || cursor < 0 || cursor >= nrecs ) {
		wattrset( wmain, COLOR_PAIR( 3 ));
		mvwaddnstr( wmain, 0, 1, "No sessions today", ww > 2 ? ww - 2 : ww );
		return;
	}
	s = &recs[cursor];
	full_addr( addr, sizeof( addr ), s );
	started = (time_t) s->started;
	{
		struct tm *tm = localtime( &started );

		if ( tm )
			snprintf( when, sizeof( when ), "%02d:%02d:%02d",
				tm->tm_hour, tm->tm_min, tm->tm_sec );
		else
			snprintf( when, sizeof( when ), "-" );
	}
	fmt_hms( dur, sizeof( dur ), s->duration );

	/* "In   :" / "Out  :" / "Addr :" — the colon stays in one column. */
	if ( 0 < h ) {
		snprintf( left, sizeof( left ), "%-5s: %s",
			( s->status & BL_IN ) ? "In" : "Out", when );
		badges = 0;
		if ( ( s->status & BL_PWD ) && ( s->status & BL_LST ))
			badges = 11;
		else if ( ( s->status & BL_PWD ) || ( s->status & BL_LST ))
			badges = 5;
		if ( badges && edge > badges ) {
			if ( s->status & BL_LST )
				draw_half( wmain, 0, edge - 5, 5, "[lst]", COLOR_PAIR( 2 ) | A_BOLD );
			if ( s->status & BL_PWD )
				draw_half( wmain, 0, edge - badges, 5, "[pwd]", COLOR_PAIR( 2 ) | A_BOLD );
			draw_lr( wmain, 0, edge - badges, left, COLOR_PAIR( 7 ), NULL, 0 );
		} else
			draw_lr( wmain, 0, edge, left, COLOR_PAIR( 7 ), NULL, 0 );
	}
	if ( 1 < h ) {
		snprintf( left, sizeof( left ), "%-5s: %s", "Addr", addr );
		draw_lr( wmain, 1, edge, left, COLOR_PAIR( 7 ), dur, COLOR_PAIR( 7 ));
	}
	if ( 2 < h ) {
		left[0] = '\0';
		if ( s->sysname[0] && s->location[0] )
			snprintf( left, sizeof( left ), "%s, %s", s->sysname, s->location );
		else if ( s->sysname[0] )
			snprintf( left, sizeof( left ), "%s", s->sysname );
		else if ( s->location[0] )
			snprintf( left, sizeof( left ), "%s", s->location );
		right[0] = '\0';
		if ( s->sysop[0] )
			snprintf( right, sizeof( right ), "Sysop: %s", s->sysop );
		draw_lr( wmain, 2, edge, left, COLOR_PAIR( 3 ), right, COLOR_PAIR( 3 ));
	}
	if ( 3 < h ) {
		wattrset( wmain, COLOR_PAIR( 2 ));
		wmove( wmain, 3, 0 );
		whline( wmain, ACS_HLINE, ww );
	}
	if ( 4 < h ) {
		left[0] = '\0';
		if ( s->peer[0] )
			snprintf( left, sizeof( left ), "Peer: %s", s->peer );
		right[0] = '\0';
		if ( !( s->status & BL_OK )) {
			if ( s->result[0] )
				snprintf( right, sizeof( right ), "Failed: %s", s->result );
			else
				snprintf( right, sizeof( right ), "Failed" );
		}
		draw_lr( wmain, 4, edge, left, COLOR_PAIR( 7 ), right, COLOR_PAIR( 4 ));
	}
	if ( 6 < h ) {
		fmt_amount( rcvd, sizeof( rcvd ), s->bytes_rcvd );
		fmt_amount( sent, sizeof( sent ), s->bytes_sent );
		draw_lr( wmain, 6, edge, "Received", COLOR_PAIR( 2 ) | A_BOLD,
			"Sent", COLOR_PAIR( 4 ) | A_BOLD );
		snprintf( left, sizeof( left ), "%u files", s->files_rcvd );
		snprintf( right, sizeof( right ), "%u files", s->files_sent );
		draw_lr( wmain, 7, edge, left, COLOR_PAIR( 7 ), right, COLOR_PAIR( 7 ));
		draw_lr( wmain, 8, edge, rcvd, COLOR_PAIR( 6 ) | A_BOLD,
			sent, COLOR_PAIR( 6 ) | A_BOLD );
	}
}
