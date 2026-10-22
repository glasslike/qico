/**********************************************************
 * T-Hist v3 frame layout.
 *
 * Byte order, offsets, CRC and the frame wrapper live here so the
 * mailer and qcc cannot drift apart. qcc links this file unless
 * configured with --disable-qcc-binlog. The mailer always links it, and
 * binlog_write() still owns the lock, the tail repair and the append.
 **********************************************************/

#include <config.h>
#include <string.h>

#include "types.h"
#include "crc.h"
#include "binlogfmt.h"

const unsigned char bl_sig[BL_SIG_LEN] = { 'H', 'I', 'S', 'T', 0x03 };

/* 50-byte header + 205 bytes of text + the reserved zero at index 255. */
typedef char bl_record_fits[( BL_OFF_STR + BL_TEXT_LEN + 1 == BL_REC_LEN ) ? 1 : -1];


void bl_put_u16(unsigned char *p, unsigned v)
{
	p[0] = (unsigned char) ( v & 0xff );
	p[1] = (unsigned char) (( v >> 8 ) & 0xff );
}


void bl_put_u32(unsigned char *p, unsigned v)
{
	p[0] = (unsigned char) ( v & 0xff );
	p[1] = (unsigned char) (( v >> 8 ) & 0xff );
	p[2] = (unsigned char) (( v >> 16 ) & 0xff );
	p[3] = (unsigned char) (( v >> 24 ) & 0xff );
}


void bl_put_u64(unsigned char *p, unsigned long long v)
{
	int i;

	for ( i = 0; i < 8; i++ ) {
		p[i] = (unsigned char) ( v & 0xff );
		v >>= 8;
	}
}


/*
 * Little-endian loads. A short image was zero-filled by bl_unpack(),
 * so reading past the stored tail yields zero rather than garbage.
 */
static unsigned bl_get_u16(const unsigned char *p)
{
	return (unsigned) p[0] | ((unsigned) p[1] << 8);
}


static unsigned bl_get_u32(const unsigned char *p)
{
	return (unsigned) p[0]
		| ((unsigned) p[1] << 8)
		| ((unsigned) p[2] << 16)
		| ((unsigned) p[3] << 24);
}


static unsigned long long bl_get_u64(const unsigned char *p)
{
	unsigned long long v = 0;
	int i;

	for ( i = 7; i >= 0; i-- ) {
		v <<= 8;
		v |= p[i];
	}
	return v;
}


unsigned bl_trim_len(const unsigned char *rec)
{
	unsigned len = 254;

	while ( len > 0 && rec[len] == 0 )
		len--;
	return len + 1;
}


unsigned bl_frame_crc(const unsigned char *len_and_image, unsigned n)
{
	return (unsigned) CRC32_FINISH( crc32block( (void *) len_and_image, n ));
}


int bl_frame_valid(const unsigned char *b, size_t n, unsigned *adv)
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
	if ( got != bl_frame_crc( b, 1 + L ))
		return 0;
	*adv = L + BL_FRAME_WRAP;
	return 1;
}


/* Copy one indexed string. BL_ABSENT and a short index yield "". */
static void copy_indexed(char *dst, size_t n, const unsigned char *text, int index)
{
	size_t i;

	if ( !n )
		return;
	dst[0] = '\0';
	if ( index < 0 || index >= BL_TEXT_LEN )
		return;
	for ( i = 0; i + 1 < n && index + (int) i < BL_TEXT_LEN; i++ ) {
		dst[i] = (char) text[index + (int) i];
		if ( dst[i] == '\0' )
			return;
	}
	dst[i] = '\0';
}


void bl_unpack(const unsigned char *image, unsigned image_len, bl_sess_t *s)
{
	unsigned char rec[BL_REC_LEN];
	const unsigned char *text;
	unsigned n;

	memset( s, 0, sizeof( *s ));
	memset( rec, 0, sizeof( rec ));
	/*
	 * Version 3 omits trailing zeros, including a string's final NUL
	 * when it was the last stored byte. The zero fill puts that NUL
	 * back. Byte 255 of the image is reserved and stays zero even if
	 * a damaged length claims it.
	 */
	n = image_len;
	if ( n > 255 )
		n = 255;
	if ( image && n )
		memcpy( rec, image, n );

	s->zone = bl_get_u16( rec + BL_OFF_ZONE );
	s->net = bl_get_u16( rec + BL_OFF_NET );
	s->node = bl_get_u16( rec + BL_OFF_NODE );
	s->point = bl_get_u16( rec + BL_OFF_POINT );
	s->started = bl_get_u64( rec + BL_OFF_START );
	s->bytes_rcvd = bl_get_u64( rec + BL_OFF_RCVD );
	s->bytes_sent = bl_get_u64( rec + BL_OFF_SENT );
	s->files_rcvd = bl_get_u32( rec + BL_OFF_NRCVD );
	s->files_sent = bl_get_u32( rec + BL_OFF_NSENT );
	s->duration = bl_get_u32( rec + BL_OFF_DUR );
	s->status = bl_get_u16( rec + BL_OFF_STATUS );

	text = rec + BL_OFF_STR;
	/* String 0 has no index byte. A successful session stores one NUL. */
	copy_indexed( s->result, sizeof( s->result ), text, 0 );
	copy_indexed( s->peer, sizeof( s->peer ), text, rec[BL_OFF_INDEX + 0] );
	copy_indexed( s->sysname, sizeof( s->sysname ), text, rec[BL_OFF_INDEX + 1] );
	copy_indexed( s->location, sizeof( s->location ), text, rec[BL_OFF_INDEX + 2] );
	copy_indexed( s->sysop, sizeof( s->sysop ), text, rec[BL_OFF_INDEX + 3] );
}
