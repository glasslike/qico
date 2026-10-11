/**********************************************************
 * T-Hist v3 binary session log.
 *
 * The file is a five-byte signature (HIST and version 3) followed
 * by variable-length frames. Each frame is a length byte, that many
 * bytes of the session image, a CRC-32/ISO-HDLC of the length byte
 * and those image bytes, and the same length byte again. The image
 * is the version 1 layout with trailing zeros removed. Byte 255 of
 * the image stays zero, so a stored frame is at most 255 bytes of
 * image plus the six-byte wrapper.
 *
 * Bytes are stored one field at a time so the image does not depend
 * on compiler padding or host endianness. The CRC is the same
 * polynomial already used for Hydra and Zmodem, finished with the
 * usual bitwise complement (the zlib crc32).
 *
 * The daemon finishes each session in its own process (a forked
 * caller, or a separate inbound qico). Writers take an exclusive
 * fcntl lock only around the append, not for the whole session.
 * The wait for that lock is capped. Past the cap this session
 * skips its record and finishes; the holder keeps the lock.
 * Under that lock an empty file gets its signature. A normal append
 * checks only the last frame. The whole file is read only when that
 * check fails; an unfinished tail is then cut back to the last frame
 * whose length bytes and CRC agree. A bad frame in the middle is not
 * deleted, so sessions after it stay in the file. A short write is
 * truncated away before the lock is released. There is no fsync. A
 * power loss can drop the last append, the same as the text history.
 **********************************************************/

#include "headers.h"
#include "binlog.h"
#include "crc.h"

/* Signature is the four letters HIST and format version 3. */
#define BL_SIG_LEN	5
#define BL_REC_LEN	256
/*
 * Usable text is 205 bytes. Record byte 255 stays zero, which version
 * 3 requires of the session image. An index >= 206 still means the
 * string is absent.
 */
#define BL_TEXT_LEN	205
#define BL_ABSENT	255
/* Length byte, CRC-32, and the repeated length byte. */
#define BL_FRAME_WRAP	6

#define BL_IN		0x0001	/* incoming session */
#define BL_OUT		0x0002	/* outgoing session */
#define BL_OK		0x0004	/* finished without an error */
#define BL_PWD		0x0008	/* password-protected session */
#define BL_LST		0x0010	/* address is in the configured nodelist */

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

static const unsigned char bl_sig[BL_SIG_LEN] = { 'H', 'I', 'S', 'T', 0x03 };

/* 50-byte header + 205 bytes of text + the reserved zero at index 255. */
typedef char bl_record_fits[( BL_OFF_STR + BL_TEXT_LEN + 1 == BL_REC_LEN ) ? 1 : -1];

/* Inbound peer from answer_mode(), or NULL on an outbound session. */
static char *peer_addr;

/* Attempt start and failure phrase. Not rnode fields. */
static time_t began_at;
static char *result_note;


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


void binlog_reset(void)
{
	xfree( result_note );
	result_note = NULL;
	began_at = time( NULL );
}


void binlog_set_result(const char *reason)
{
	xfree( result_note );
	result_note = NULL;
	if ( reason && *reason )
		result_note = xstrdup( reason );
}


const char *binlog_noted_result(void)
{
	return result_note;
}


time_t binlog_began(void)
{
	return began_at;
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


/*
 * How long a session will wait for another writer. A live append is
 * a few milliseconds. The cap covers a slow repair of a damaged tail.
 * After that this session gives up its own record. It does not break
 * the other process's lock: that process may be inside a write, and
 * killing it would also skip the node status update that follows.
 */
#define BL_LOCK_WAIT_MS		30000
#define BL_LOCK_PAUSE_MS	100

/*
 * Whole-file advisory lock. 0 means we hold it. -1 is a real error.
 * 1 means the wait expired and the caller must not write.
 * F_SETLK returns at once when the lock is busy (EAGAIN or EACCES),
 * so a stuck holder cannot park this session inside the kernel.
 * The kernel still drops the lock if the holder dies.
 */
static int lock_file(int fd)
{
	struct flock fl;
	int waited = 0;

	memset( &fl, 0, sizeof( fl ));
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;
	for ( ;; ) {
		if ( fcntl( fd, F_SETLK, &fl ) == 0 )
			return 0;
		if ( errno == EINTR )
			continue;
		if ( errno != EAGAIN && errno != EACCES )
			return -1;
		if ( waited >= BL_LOCK_WAIT_MS )
			return 1;
		qsleep( BL_LOCK_PAUSE_MS );
		waited += BL_LOCK_PAUSE_MS;
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
		int inbound, int successful, int password, int listed,
		const char *result, const char *peer, const char *sysname,
		const char *location, const char *sysop)
{
	unsigned char *text = rec + BL_OFF_STR;
	unsigned status;
	int used = 0;

	memset( rec, 0, BL_REC_LEN );

	/*
	 * No address leaves the four fields zero. The layout has no
	 * "absent" flag, so a reader displays that as 0:0/0.0.
	 */
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

	/* Exactly one direction bit. Listed is T-Hist status bit 4. */
	status = inbound ? BL_IN : BL_OUT;
	if ( successful )
		status |= BL_OK;
	if ( password )
		status |= BL_PWD;
	if ( listed )
		status |= BL_LST;
	put_u16( rec + BL_OFF_STATUS, status );

	/*
	 * String 0 is the failure phrase. A successful session leaves it
	 * empty: Strings[0] is a zero byte, and T-Hist treats that missing
	 * text as success (no 'A' mark). String 1 is the peer address and
	 * then starts at Strings[1]. A failure stores its phrase at
	 * Strings[0], so the address follows that phrase. An empty peer
	 * stays absent rather than becoming an empty field. Then system
	 * name, location and sysop.
	 */
	if ( append_str( text, &used, result ) == BL_ABSENT ) {
		text[0] = '\0';
		used = 1;
	}
	rec[BL_OFF_INDEX + 0] = (unsigned char) append_str( text, &used, peer );
	rec[BL_OFF_INDEX + 1] = (unsigned char) append_str( text, &used, sysname );
	rec[BL_OFF_INDEX + 2] = (unsigned char) append_str( text, &used, location );
	rec[BL_OFF_INDEX + 3] = (unsigned char) append_str( text, &used, sysop );
}


/*
 * Version 3 stores the image only through its last non-zero byte, and
 * never stores byte 255. The walk starts at 254 for that reason. A
 * completely zero image still yields length 1.
 */
static unsigned trim_len(const unsigned char *rec)
{
	unsigned len = 254;

	while ( len > 0 && rec[len] == 0 )
		len--;
	return len + 1;
}


/* CRC-32/ISO-HDLC of the length byte and the image bytes that follow it. */
static unsigned frame_crc(const unsigned char *len_and_image, unsigned n)
{
	return (unsigned) CRC32_FINISH( crc32block( (void *) len_and_image, n ));
}


/*
 * 1: a whole frame with matching length bytes and CRC.
 * 0: these bytes are not a frame.
 * -1: the buffer ends before the claimed frame does.
 */
static int frame_valid(const unsigned char *b, size_t n, unsigned *adv)
{
	unsigned L, got;

	if ( n < 1 )
		return -1;
	L = b[0];
	if ( L < 1 || L > 255 )
		return 0;
	if ( n < (size_t) L + BL_FRAME_WRAP )
		return -1;
	if ( b[L + 5] != (unsigned char) L )
		return 0;
	got = (unsigned) b[1 + L]
		| ((unsigned) b[2 + L] << 8)
		| ((unsigned) b[3 + L] << 16)
		| ((unsigned) b[4 + L] << 24);
	if ( got != frame_crc( b, 1 + L ))
		return 0;
	*adv = L + BL_FRAME_WRAP;
	return 1;
}


/* The usual append. One short read at the end of the file, no scan. */
static int last_frame_good(int fd, off_t end)
{
	unsigned char b[1 + 255 + 4 + 1];
	unsigned char L;
	unsigned adv;
	off_t start;

	if ( end < (off_t) BL_SIG_LEN + 1 + BL_FRAME_WRAP )
		return 0;
	if ( lseek( fd, end - 1, SEEK_SET ) < 0 || read_full( fd, &L, 1 ) < 0 )
		return 0;
	if ( L < 1 )
		return 0;
	start = end - (off_t) ( L + BL_FRAME_WRAP );
	if ( start < (off_t) BL_SIG_LEN )
		return 0;
	if ( lseek( fd, start, SEEK_SET ) < 0
		|| read_full( fd, b, (size_t) L + BL_FRAME_WRAP ) < 0 )
		return 0;
	return frame_valid( b, (size_t) L + BL_FRAME_WRAP, &adv ) == 1;
}


/*
 * Walk from the signature to the last frame whose length and CRC
 * agree. A damaged frame is skipped a byte at a time so a later good
 * frame is kept. An unfinished tail stops the walk. Returns that end
 * offset, or -1 if the file could not be read.
 */
static off_t scan_good_end(int fd, off_t end)
{
	unsigned char b[1 + 255 + 4 + 1];
	off_t pos = BL_SIG_LEN;
	off_t good = BL_SIG_LEN;

	while ( pos < end ) {
		off_t remain = end - pos;
		size_t n = remain > (off_t) sizeof( b ) ? sizeof( b ) : (size_t) remain;
		unsigned adv = 0;
		int v;

		if ( lseek( fd, pos, SEEK_SET ) < 0 || read_full( fd, b, n ) < 0 )
			return -1;
		v = frame_valid( b, n, &adv );
		if ( v == 1 ) {
			pos += (off_t) adv;
			good = pos;
			continue;
		}
		/*
		 * The length byte claims more bytes than the file has.
		 * That is an unfinished tail only when no later offset
		 * still holds a whole frame. A large garbage length in
		 * front of a good frame must not eat that frame.
		 */
		if ( v < 0 ) {
			off_t later = pos + 1;
			int found = 0;

			while ( later < end && (end - later) >= (off_t) ( 1 + BL_FRAME_WRAP )) {
				size_t ln;
				unsigned ladv = 0;
				int lv;

				ln = (size_t) ( end - later );
				if ( ln > sizeof( b ))
					ln = sizeof( b );
				if ( lseek( fd, later, SEEK_SET ) < 0
					|| read_full( fd, b, ln ) < 0 )
					return -1;
				lv = frame_valid( b, ln, &ladv );
				if ( lv == 1 ) {
					found = 1;
					break;
				}
				later++;
			}
			if ( !found )
				break;
			/* Skip the false length and resume at the later frame. */
			pos = later;
			continue;
		}
		pos++;
	}
	return good;
}


void binlog_write(const char *path, const ftnaddr_t *addr,
		time_t started, time_t duration,
		long bytes_sent, long bytes_rcvd,
		int files_sent, int files_rcvd,
		int inbound, int successful, int password, int listed,
		const char *result, const char *peer, const char *sysname,
		const char *location, const char *sysop)
{
	unsigned char rec[BL_REC_LEN];
	unsigned char frame[1 + 255 + 4 + 1];
	unsigned char sig[BL_SIG_LEN];
	char pathcopy[MAX_PATH + 1];
	unsigned plen, crc;
	off_t end, good;
	int fd;

	if ( !path || !*path )
		return;

	/* ccs is global and the next cfgs() call replaces it. */
	xstrcpy( pathcopy, path, sizeof( pathcopy ));

	fill_record( rec, addr, started, duration,
		bytes_sent, bytes_rcvd, files_sent, files_rcvd,
		inbound, successful, password, listed,
		result, peer, sysname, location, sysop );

	fd = open( pathcopy, O_RDWR | O_CREAT, 0666 );
	if ( fd < 0 ) {
		write_log( "binlog: can't open '%s': %s", pathcopy, strerror( errno ));
		return;
	}
	{
		int locked = lock_file( fd );

		if ( locked > 0 ) {
			write_log( "binlog: lock timed out on '%s'", pathcopy );
			close( fd );
			return;
		}
		if ( locked < 0 ) {
			write_log( "binlog: can't lock '%s': %s", pathcopy, strerror( errno ));
			close( fd );
			return;
		}
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
			write_log( "binlog: '%s' is not a T-Hist v3 log, not writing",
				pathcopy );
			unlock_file( fd );
			close( fd );
			return;
		}
		/*
		 * A matching last frame means the tail is whole. Anything
		 * else can be a short write. Find the last good frame and
		 * drop only what follows it.
		 */
		if ( end > (off_t) BL_SIG_LEN && !last_frame_good( fd, end )) {
			good = scan_good_end( fd, end );
			if ( good < 0 ) {
				write_log( "binlog: can't repair tail of '%s': %s",
					pathcopy, strerror( errno ));
				unlock_file( fd );
				close( fd );
				return;
			}
			if ( good < end ) {
				if ( ftruncate( fd, good ) < 0 ) {
					write_log( "binlog: can't repair tail of '%s': %s",
						pathcopy, strerror( errno ));
					unlock_file( fd );
					close( fd );
					return;
				}
				write_log( "binlog: dropped incomplete tail of '%s'", pathcopy );
				end = good;
			}
		}
	}

	plen = trim_len( rec );
	frame[0] = (unsigned char) plen;
	memcpy( frame + 1, rec, plen );
	crc = frame_crc( frame, 1 + plen );
	put_u32( frame + 1 + plen, crc );
	frame[1 + plen + 4] = (unsigned char) plen;

	if ( lseek( fd, end, SEEK_SET ) < 0
		|| write_full( fd, frame, (size_t) plen + BL_FRAME_WRAP ) < 0 ) {
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
