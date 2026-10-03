/**********************************************************
 * T-Hist v1 binary session log.
 *
 * Include after headers.h. The text history file is a separate
 * option and is not touched here.
 **********************************************************/
#ifndef __BINLOG_H__
#define __BINLOG_H__

/*
 * Remember the inbound peer address until the session record is
 * written. answer_mode() learns it from getpeername() into a stack
 * buffer that session() cannot see. This copy is not rnode->host:
 * that field is the dial or nodelist target, and config `host'
 * expressions test it.
 *
 * Outbound sessions leave this unset and the writer uses rnode->host.
 * An empty or NULL peer clears the saved address.
 */
void		binlog_set_peer(const char *peer);
const char	*binlog_peer(void);

/*
 * Remember when this process started the attempt, and an optional
 * failure phrase. Both live only in the binary log. rnode->starttime
 * stays the handshake time used by history, qcc and Perl.
 *
 * binlog_reset() clears a phrase left by an older attempt in this
 * process and does not clear the peer address: answer_mode() stores
 * that before session() runs.
 */
void		binlog_reset(void);
void		binlog_set_result(const char *reason);
const char	*binlog_noted_result(void);
time_t		binlog_began(void);

/*
 * Append one T-Hist v1 record. path comes from the `binlog' keyword.
 * A NULL or empty path returns without creating a file.
 *
 * addr NULL means the FTN address is not known yet. The four address
 * fields cannot be omitted; they stay zero, which a reader shows as
 * 0:0/0.0. Do not pass a made-up address. bytes_sent and bytes_rcvd
 * are the same 32-bit session totals the text history writes
 * (toff - stot); they are zero-extended into the 64-bit fields.
 * inbound, successful, password and listed are booleans.
 * `listed' is the same O_LST flag the text history writes as "L"
 * (on BinkP it is set only when binkplisted is on).
 *
 * result is the first text field. T-Hist joins the fields with "; ",
 * so the phrase is stored without a separator of its own. The caller
 * passes "OK" for a session whose result is S_OK, a hangup phrase
 * ("carrier lost", "hangup", "session limit", "low cps") when one is
 * known, the short abort phrase noted with binlog_set_result(), or
 * "failed". peer is the IP address and is omitted when empty, rather
 * than stored as an empty field. Then system name, location and sysop.
 * A failure to write is logged and does not fail the session.
 */
void		binlog_write(const char *path, const ftnaddr_t *addr,
			time_t started, time_t duration,
			long bytes_sent, long bytes_rcvd,
			int files_sent, int files_rcvd,
			int inbound, int successful, int password, int listed,
			const char *result, const char *peer,
			const char *sysname, const char *location,
			const char *sysop);

#endif
