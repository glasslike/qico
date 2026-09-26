/**********************************************************
 * T-Hist v1 binary session log.
 *
 * The file is a five-byte signature (HIST and version 1) followed
 * by fixed 256-byte little-endian records. T-Hist 1.4 reads this
 * layout. Bytes are stored one field at a time so the record does
 * not depend on compiler padding or host endianness.
 *
 * The daemon finishes each session in its own process (a forked
 * caller, or a separate inbound qico). Writers take an exclusive
 * fcntl lock only around the append, not for the whole session.
 * Under that lock an empty file gets its signature, a torn tail
 * (size other than 5 + 256*N) is cut back to the last whole
 * record, and the new record is one write loop. A short write is
 * truncated away before the lock is released, so the next session
 * still sees a record boundary. A kill in the middle of write()
 * can still leave a short tail; the next writer drops it.
 *
 * There is no fsync. A power loss can drop the last append, the
 * same as the text history file.
 **********************************************************/

#include "headers.h"
#include "binlog.h"

/* Signature is the four letters HIST and format version 1. */
#define BL_SIG_LEN	5
#define BL_REC_LEN	256
#define BL_TEXT_LEN	206
/* Index byte meaning "this string is not stored". Spec: >= 206. */
#define BL_ABSENT	255

#define BL_IN		0x0001	/* incoming session */
#define BL_OUT		0x0002	/* outgoing session */
#define BL_OK		0x0004	/* finished without an error */
#define BL_PWD		0x0008	/* password-protected session */

/* Offsets inside the 256-byte record. */
#define BL_OFF_ZONE	0
#define BL_OFF_NET	2
#define BL_OFF_NODE	4
#define BL_OFF_POINT	6
#define BL_OFF_START	8
#define BL_OFF_RCVD	16
#define BL_OFF_SENT	24
#define BL_OFF_NRCVD	32
#define BL_OFF_NSENT	36
#define BL_OFF_DUR	40
#define BL_OFF_STATUS	44
#define BL_OFF_INDEX	46
#define BL_OFF_STR	50

static const unsigned char bl_sig[BL_SIG_LEN] = { 'H', 'I', 'S', 'T', 0x01 };

/* 8-byte address + 3*8 traffic/time + 3*4 counts + 2 status + 4 index + 206 text. */
typedef char bl_record_fits[( BL_OFF_STR + BL_TEXT_LEN == BL_REC_LEN ) ? 1 : -1];

/* Inbound peer from answer_mode(), or NULL on an outbound session. */
static char *peer_addr;


void binlog_set_peer(const char *peer)
{
	xfree( peer_addr );
	if ( peer && *peer )
		peer_addr = xstrdup( peer );
}


const char *binlog_peer(void)
{
	return peer_addr;
}


static void put_u16(unsigned char *p, unsigned v)
{
	p[0] = (unsigned char) ( v & 0xff );
	p[1] = (unsigned char) (( v >> 8 ) & 0xff );
}


static void put_u32(unsigned char *p, unsigned v)
{
	p[0] = (unsigned char) ( v & 0xff );
	p[1] = (unsigned char) (( v >> 8 ) & 0xff );
	p[2] = (unsigned char) (( v >> 16 ) & 0xff );
	p[3] = (unsigned char) (( v >> 24 ) & 0xff );
}


static void put_u64(unsigned char *p, unsigned long long v)
{
	int i;

	for ( i = 0; i < 8; i++ ) {
		p[i] = (unsigned char) ( v & 0xff );
		v >>= 8;
	}
}


/* FTN address fields are 16-bit in this record. */
static unsigned ftn16(int v)
{
	if ( v < 0 )
		return 0;
	return (unsigned) v & 0xffffu;
}


/*
 * Session counters are 32-bit ints. Store that bit pattern in the
 * 64-bit field. A wrapped negative value must not be sign-extended
 * into the top half.
 */
static unsigned long long u64_bytes(long v)
{
	return (unsigned long long) (unsigned int) v;
}


static unsigned long long u64_time(time_t t)
{
	if ( t <= 0 )
		return 0;
	return (unsigned long long) t;
}


static unsigned u32_duration(time_t duration)
{
	if ( duration <= 0 )
		return 0;
	if ( (unsigned long long) duration > 0xffffffffull )
		return 0xffffffffu;
	return (unsigned) duration;
}


static unsigned u32_files(int n)
{
	if ( n < 0 )
		return 0;
	return (unsigned) n;
}


/*
 * Append one NUL-terminated string at strings[*used]. Returns the
 * start offset, or BL_ABSENT when the source is empty or nothing
 * fits. A string that does not fit is cut to the remaining room.
 */
static int append_str(unsigned char *strings, int *used, const char *s)
{
	size_t n;
	int start, room;

	if ( !s || !*s || *used >= BL_TEXT_LEN )
		return BL_ABSENT;
	room = BL_TEXT_LEN - *used;
	if ( room < 2 )
		return BL_ABSENT;
	n = strlen( s );
	if ( n > (size_t) ( room - 1 ))
		n = (size_t) ( room - 1 );
	start = *used;
	memcpy( strings + start, s, n );
	strings[start + (int) n] = '\0';
	*used = start + (int) n + 1;
	return start;
}


static int write_full(int fd, const void *buf, size_t len)
{
	const unsigned char *p = (const unsigned char *) buf;

	while ( len ) {
		ssize_t n = write( fd, p, len );

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


/* Whole-file advisory lock. Released by the kernel if this process dies. */
static int lock_file(int fd)
{
	struct flock fl;

	memset( &fl, 0, sizeof( fl ));
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;
	for ( ;; ) {
		if ( fcntl( fd, F_SETLKW, &fl ) == 0 )
			return 0;
		if ( errno != EINTR )
			return -1;
	}
}


static void unlock_file(int fd)
{
	struct flock fl;

	memset( &fl, 0, sizeof( fl ));
	fl.l_type = F_UNLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;
	while ( fcntl( fd, F_SETLK, &fl ) < 0 && errno == EINTR )
		;
}


static void fill_record(unsigned char *rec, const ftnaddr_t *addr,
		time_t started, time_t duration,
		long bytes_sent, long bytes_rcvd,
		int files_sent, int files_rcvd,
		int inbound, int successful, int password,
		const char *peer, const char *sysname,
		const char *location, const char *sysop)
{
	unsigned char *text = rec + BL_OFF_STR;
	unsigned status;
	int used = 0;

	memset( rec, 0, BL_REC_LEN );

	put_u16( rec + BL_OFF_ZONE, addr ? ftn16( addr->z ) : 0 );
	put_u16( rec + BL_OFF_NET, addr ? ftn16( addr->n ) : 0 );
	put_u16( rec + BL_OFF_NODE, addr ? ftn16( addr->f ) : 0 );
	put_u16( rec + BL_OFF_POINT, addr ? ftn16( addr->p ) : 0 );
	put_u64( rec + BL_OFF_START, u64_time( started ));
	put_u64( rec + BL_OFF_RCVD, u64_bytes( bytes_rcvd ));
	put_u64( rec + BL_OFF_SENT, u64_bytes( bytes_sent ));
	put_u32( rec + BL_OFF_NRCVD, u32_files( files_rcvd ));
	put_u32( rec + BL_OFF_NSENT, u32_files( files_sent ));
	put_u32( rec + BL_OFF_DUR, u32_duration( duration ));

	/* Exactly one direction bit. Listed is not stored. */
	status = inbound ? BL_IN : BL_OUT;
	if ( successful )
		status |= BL_OK;
	if ( password )
		status |= BL_PWD;
	put_u16( rec + BL_OFF_STATUS, status );

	/*
	 * String 0 is the peer address (or empty). String 1, the domain
	 * name, is left absent: qico does not have that name on its own.
	 * Then system name, location, sysop.
	 */
	if ( append_str( text, &used, peer ) == BL_ABSENT ) {
		text[0] = '\0';
		used = 1;
	}
	rec[BL_OFF_INDEX + 0] = (unsigned char) BL_ABSENT;
	rec[BL_OFF_INDEX + 1] = (unsigned char) append_str( text, &used, sysname );
	rec[BL_OFF_INDEX + 2] = (unsigned char) append_str( text, &used, location );
	rec[BL_OFF_INDEX + 3] = (unsigned char) append_str( text, &used, sysop );
}


void binlog_write(const char *path, const ftnaddr_t *addr,
		time_t started, time_t duration,
		long bytes_sent, long bytes_rcvd,
		int files_sent, int files_rcvd,
		int inbound, int successful, int password,
		const char *peer, const char *sysname,
		const char *location, const char *sysop)
{
	unsigned char rec[BL_REC_LEN];
	unsigned char sig[BL_SIG_LEN];
	char pathcopy[MAX_PATH + 1];
	off_t end, keep;
	int fd;

	if ( !path || !*path )
		return;

	/* ccs is global and the next cfgs() call replaces it. */
	xstrcpy( pathcopy, path, sizeof( pathcopy ));

	fill_record( rec, addr, started, duration,
		bytes_sent, bytes_rcvd, files_sent, files_rcvd,
		inbound, successful, password,
		peer, sysname, location, sysop );

	fd = open( pathcopy, O_RDWR | O_CREAT, 0666 );
	if ( fd < 0 ) {
		write_log( "binlog: can't open '%s': %s", pathcopy, strerror( errno ));
		return;
	}
	if ( lock_file( fd ) < 0 ) {
		write_log( "binlog: can't lock '%s': %s", pathcopy, strerror( errno ));
		close( fd );
		return;
	}

	end = lseek( fd, 0, SEEK_END );
	if ( end < 0 ) {
		write_log( "binlog: can't seek '%s': %s", pathcopy, strerror( errno ));
		unlock_file( fd );
		close( fd );
		return;
	}

	if ( end == 0 ) {
		if ( write_full( fd, bl_sig, BL_SIG_LEN ) < 0 ) {
			int saved = errno;

			ftruncate( fd, 0 );
			write_log( "binlog: can't write signature to '%s': %s",
				pathcopy, strerror( saved ));
			unlock_file( fd );
			close( fd );
			return;
		}
		end = BL_SIG_LEN;
	} else {
		if ( lseek( fd, 0, SEEK_SET ) < 0
			|| read_full( fd, sig, BL_SIG_LEN ) < 0
			|| memcmp( sig, bl_sig, BL_SIG_LEN ) != 0 ) {
			write_log( "binlog: '%s' is not a T-Hist v1 log, not writing",
				pathcopy );
			unlock_file( fd );
			close( fd );
			return;
		}
		/*
		 * A previous writer died inside write() and left a short
		 * tail. Drop it so this record stays on a 256-byte boundary.
		 */
		if ( ( end - BL_SIG_LEN ) % BL_REC_LEN != 0 ) {
			keep = BL_SIG_LEN + (( end - BL_SIG_LEN ) / BL_REC_LEN ) * BL_REC_LEN;
			if ( ftruncate( fd, keep ) < 0 ) {
				write_log( "binlog: can't repair tail of '%s': %s",
					pathcopy, strerror( errno ));
				unlock_file( fd );
				close( fd );
				return;
			}
			write_log( "binlog: dropped incomplete tail of '%s'", pathcopy );
			end = keep;
		}
	}

	if ( lseek( fd, end, SEEK_SET ) < 0
		|| write_full( fd, rec, BL_REC_LEN ) < 0 ) {
		int saved = errno;

		/*
		 * Roll back a partial record before anybody else appends.
		 * If we die here, the next session repairs the same tail.
		 */
		ftruncate( fd, end );
		write_log( "binlog: short write on '%s': %s", pathcopy, strerror( saved ));
	}

	unlock_file( fd );
	close( fd );
}
