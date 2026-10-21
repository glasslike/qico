/**********************************************************
 * T-Hist v3 frame layout.
 *
 * Shared by the mailer writer (binlog.c) and, when qcc is built
 * with --enable-qcc-binlog, by the history reader. Nothing here
 * opens a file, takes a lock, or logs. The writer still decides
 * when a frame is appended and still repairs a torn tail itself.
 *
 * The image is 256 bytes. Stored frames drop trailing zero bytes
 * and never include image byte 255, which stays zero. On disk a
 * frame is: length byte, that many image bytes, CRC-32/ISO-HDLC
 * of the length and those bytes, and the same length byte again.
 **********************************************************/
#ifndef __BINLOGFMT_H__
#define __BINLOGFMT_H__

#include <stddef.h>

/* Signature is the four letters HIST and format version 3. */
#define BL_SIG_LEN	5
#define BL_REC_LEN	256
/*
 * Usable text is 205 bytes (indexes 0..204). Record byte 255 stays
 * zero, which version 3 requires of the session image. An index
 * >= BL_TEXT_LEN, including BL_ABSENT, means the string is absent.
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

/* One unpacked session. Strings are NUL-terminated and may be empty. */
typedef struct {
	unsigned	zone, net, node, point;
	unsigned long long started;
	unsigned long long bytes_rcvd, bytes_sent;
	unsigned	files_rcvd, files_sent;
	unsigned	duration;
	unsigned	status;
	char		result[BL_TEXT_LEN + 1];
	char		peer[BL_TEXT_LEN + 1];
	char		sysname[BL_TEXT_LEN + 1];
	char		location[BL_TEXT_LEN + 1];
	char		sysop[BL_TEXT_LEN + 1];
} bl_sess_t;

extern const unsigned char bl_sig[BL_SIG_LEN];

/* Little-endian stores. The image does not depend on host endianness. */
void		bl_put_u16(unsigned char *p, unsigned v);
void		bl_put_u32(unsigned char *p, unsigned v);
void		bl_put_u64(unsigned char *p, unsigned long long v);

/*
 * Image length actually stored. Never returns 0 or 256. A completely
 * zero image still yields 1, and byte 255 is never included.
 */
unsigned	bl_trim_len(const unsigned char *rec);

/* CRC-32/ISO-HDLC of the length byte and the image bytes that follow it. */
unsigned	bl_frame_crc(const unsigned char *len_and_image, unsigned n);

/*
 * 1: a whole frame with matching length bytes and CRC.
 * 0: these bytes are not a frame.
 * -1: the buffer ends before the claimed frame does.
 * On success *adv is the frame size, including both length bytes and the CRC.
 */
int		bl_frame_valid(const unsigned char *b, size_t n, unsigned *adv);

/*
 * Expand a trimmed image into the fixed fields. Missing tail bytes are
 * zero, which is what trim_len removed. String index 255 means absent.
 */
void		bl_unpack(const unsigned char *image, unsigned image_len, bl_sess_t *s);

#endif
