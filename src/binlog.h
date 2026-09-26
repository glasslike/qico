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
 * Append one T-Hist v1 record. path comes from the `binlog' keyword.
 * A NULL or empty path returns without creating a file.
 *
 * addr may be NULL (stored as 0:0/0.0). bytes_sent and bytes_rcvd are
 * the same 32-bit session totals the text history writes
 * (toff - stot); they are zero-extended into the 64-bit fields.
 * inbound, successful and password are booleans. The five text slots
 * are peer address, no domain name, system name, location, sysop.
 * A failure to write is logged and does not fail the session.
 */
void		binlog_write(const char *path, const ftnaddr_t *addr,
			time_t started, time_t duration,
			long bytes_sent, long bytes_rcvd,
			int files_sent, int files_rcvd,
			int inbound, int successful, int password,
			const char *peer, const char *sysname,
			const char *location, const char *sysop);

#endif
