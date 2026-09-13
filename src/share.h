/**********************************************************
 * Shared AKA (binkd-compatible "share") for failover netmail.
 *
 * Config:  share <shared-aka> <member1> [member2 ...]
 * Example: share 2:999/999 2:203/0 2:292/854
 *
 * Behaviour mirrors binkd:
 *  - outbound for the shared AKA is scanned;
 *  - queue/poll fan-out attributes that mail to each member;
 *  - on session with a member, remote ADR/AKA list is adjusted
 *    (strip shared if claimed; append shared if peer is a member);
 *  - uncompressed .pkt destined to the shared AKA is rewritten
 *    (dest + pkt password) to the session peer while sending.
 **********************************************************/
#ifndef __SHARE_H__
#define __SHARE_H__

#include "ftn.h"
#include "slists.h"

/* Build / free the runtime share table from CFG_SHARE. */
void	share_init(void);
void	share_done(void);

/* True if fa is a configured shared AKA. */
int	share_is_shared(const ftnaddr_t *fa);

/* True if fa is listed as a member of any share chain. */
int	share_is_member(const ftnaddr_t *fa);

/*
 * Queue fan-out callback: for each share whose shared AKA equals fa,
 * invoke each() once per member with the same file attributes.
 * Safe against nested share definitions that would recurse forever
 * (fan-out only when fa is the shared address, never when it is a member).
 */
void	share_fanout(const char *fname, const ftnaddr_t *fa,
		int type, int flavor, int rslow, qeach_t each);

/*
 * BinkP M_ADR helper: take remote ADR text, blank out any shared AKA the
 * peer claimed as theirs, and append shared AKA(s) when the peer is a
 * share member. Returns a malloc'd string (always); caller must free.
 */
char	*share_adr_adjust(const char *s);

/*
 * EMSI (and any falist-based) helper: same rules as share_adr_adjust,
 * applied in-place to a list of remote addresses.
 */
void	share_remote_falist(falist_t **addrs);

/*
 * If tosend is an uncompressed .pkt whose header dest is a shared AKA,
 * build a temp copy with dest/password rewritten to the session peer and
 * return an open FILE* on that temp. Sets *rewritten to 1 as soon as a
 * rewrite is required (even if creating the temp later fails — caller must
 * not fall back to the original file). On success *tmp_path is set (caller
 * deletes after close). Returns NULL when no rewrite is needed, or when a
 * required rewrite failed.
 */
FILE	*share_open_rewritten_pkt(const char *tosend, char **tmp_path, int *rewritten);

/* First non-shared address from rnode (session peer for pkt rewrite). */
const ftnaddr_t	*share_session_peer(void);

#endif
