/**********************************************************
 * Shared AKA (binkd-compatible "share") for failover netmail.
 **********************************************************/

#include "headers.h"
#include "share.h"

/* One member address in a share chain. */
typedef struct share_member {
	ftnaddr_t		addr;
	struct share_member	*next;
} share_member_t;

/* One "share <sha> <members...>" definition. */
typedef struct share_chain {
	ftnaddr_t		sha;
	share_member_t		*members;
	struct share_chain	*next;
} share_chain_t;

static share_chain_t *share_chains = NULL;

/* Free one member list. */
static void share_free_members(share_member_t *m)
{
	while ( m ) {
		share_member_t *n = m->next;
		xfree( m->addr.d );
		xfree( m );
		m = n;
	}
}


void share_done(void)
{
	while ( share_chains ) {
		share_chain_t *n = share_chains->next;
		share_free_members( share_chains->members );
		xfree( share_chains->sha.d );
		xfree( share_chains );
		share_chains = n;
	}
}


/*
 * Parse CFG_SHARE entries (stored as C_ADRSTRL: addr = shared AKA,
 * str = space-separated member list) into share_chains.
 */
void share_init(void)
{
	faslist_t	*s;
	char		*buf, *p, *tok;
	FTNADDR_T	( ma );
	share_chain_t	*ch;
	share_member_t	*m, **mp;

	share_done();

	for ( s = cfgfasl( CFG_SHARE ); s; s = s->next ) {
		if ( !s->str || !*s->str ) {
			write_log( "share %s: no member addresses", ftnaddrtoa( &s->addr ));
			continue;
		}

		ch = xcalloc( 1, sizeof( *ch ));
		addr_cpy( &ch->sha, &s->addr );
		mp = &ch->members;

		buf = xstrdup( s->str );
		for ( p = buf; ( tok = strsep( &p, " \t" )); ) {
			if ( !*tok )
				continue;
			if ( !parseftnaddr( tok, &ma, &DEFADDR, 0 )) {
				write_log( "share %s: bad member address '%s'",
					ftnaddrtoa( &s->addr ), tok );
				continue;
			}
			m = xcalloc( 1, sizeof( *m ));
			addr_cpy( &m->addr, &ma );
			*mp = m;
			mp = &m->next;
		}
		xfree( buf );

		if ( !ch->members ) {
			write_log( "share %s: no valid members, ignored",
				ftnaddrtoa( &s->addr ));
			xfree( ch );
			continue;
		}

		ch->next = share_chains;
		share_chains = ch;
		DEBUG(('C',2,"share: %s -> members configured", ftnaddrtoa( &ch->sha )));
	}
}


int share_is_shared(const ftnaddr_t *fa)
{
	share_chain_t *ch;

	if ( !fa )
		return 0;
	for ( ch = share_chains; ch; ch = ch->next )
		if ( addr_cmp( fa, &ch->sha ))
			return 1;
	return 0;
}


int share_is_member(const ftnaddr_t *fa)
{
	share_chain_t	*ch;
	share_member_t	*m;

	if ( !fa )
		return 0;
	for ( ch = share_chains; ch; ch = ch->next )
		for ( m = ch->members; m; m = m->next )
			if ( addr_cmp( fa, &m->addr ))
				return 1;
	return 0;
}


void share_fanout(const char *fname, const ftnaddr_t *fa,
		int type, int flavor, int rslow, qeach_t each)
{
	share_chain_t	*ch;
	share_member_t	*m;

	if ( !fa || !each || !share_chains )
		return;

	for ( ch = share_chains; ch; ch = ch->next ) {
		if ( !addr_cmp( fa, &ch->sha ))
			continue;
		for ( m = ch->members; m; m = m->next )
			each( fname, &m->addr, type, flavor, rslow );
	}
}


/*
 * Match binkd add_shared_akas() + ADR() concat:
 *  - blank shared AKAs the peer claimed as theirs;
 *  - if peer presents a member address, append the shared AKA.
 * Always returns a malloc'd string; caller frees.
 */
char *share_adr_adjust(const char *s)
{
	char		*work, *toks, *save, *w, *c, *ad = NULL;
	FTNADDR_T	( fa );
	share_chain_t	*ch;
	share_member_t	*m;
	size_t		adlen = 0;

	if ( !s )
		return xstrdup( "" );
	if ( !share_chains )
		return xstrdup( s );

	/* Mutable copy: blank shared claims in place (binkd style). */
	work = xstrdup( s );
	save = toks = xstrdup( s );

	while (( w = strsep( &toks, " " ))) {
		if ( !*w || !parseftnaddr( w, &fa, NULL, 0 ))
			continue;

		for ( ch = share_chains; ch; ch = ch->next ) {
			if ( addr_cmp( &fa, &ch->sha )) {
				c = strstr( work, w );
				if ( c ) {
					memset( c, ' ', strlen( w ));
					write_log( "shared aka `%s' used by node %s",
						ftnaddrtoa( &fa ), s );
				}
				break;
			}
			for ( m = ch->members; m; m = m->next ) {
				if ( !addr_cmp( &fa, &m->addr ))
					continue;
				{
					const char *sha = ftnaddrtoa( &ch->sha );
					size_t sl = strlen( sha );
					ad = xrealloc( ad, adlen + sl + 2 );
					ad[adlen++] = ' ';
					memcpy( ad + adlen, sha, sl + 1 );
					adlen += sl;
					write_log( "shared aka %s is added", sha );
				}
				break;
			}
		}
	}
	xfree( save );

	if ( ad ) {
		work = xrealloc( work, strlen( work ) + adlen + 1 );
		strcat( work, ad );
		xfree( ad );
	}
	return work;
}


void share_remote_falist(falist_t **addrs)
{
	falist_t	*a, **pp;
	share_chain_t	*ch;
	share_member_t	*m;
	falist_t	*add_list = NULL;

	if ( !addrs || !*addrs || !share_chains )
		return;

	/* Strip shared AKAs claimed by the remote. */
	pp = addrs;
	while (( a = *pp )) {
		if ( share_is_shared( &a->addr )) {
			write_log( "shared aka `%s' used by remote, removed",
				ftnaddrtoa( &a->addr ));
			*pp = a->next;
			xfree( a->addr.d );
			xfree( a );
			continue;
		}
		pp = &a->next;
	}

	/* If any remaining aka is a member, append the shared AKA. */
	for ( a = *addrs; a; a = a->next ) {
		for ( ch = share_chains; ch; ch = ch->next ) {
			for ( m = ch->members; m; m = m->next ) {
				if ( !addr_cmp( &a->addr, &m->addr ))
					continue;
				if ( !falist_find( *addrs, &ch->sha )
					&& !falist_find( add_list, &ch->sha )) {
					falist_add( &add_list, &ch->sha );
					write_log( "shared aka %s is added",
						ftnaddrtoa( &ch->sha ));
				}
				break;
			}
		}
	}

	while ( add_list ) {
		falist_t *n = add_list->next;
		falist_add( addrs, &add_list->addr );
		xfree( add_list->addr.d );
		xfree( add_list );
		add_list = n;
	}
}


const ftnaddr_t *share_session_peer(void)
{
	falist_t *a;

	if ( !rnode )
		return NULL;

	/* Prefer the first remote aka that is not a shared address. */
	for ( a = rnode->addrs; a; a = a->next )
		if ( !share_is_shared( &a->addr ))
			return &a->addr;

	return rnode->addrs ? &rnode->addrs->addr : NULL;
}


/*
 * Read pkt dest (type-2 / 2+) into fa. Returns 0 if not a version-2 pkt.
 */
static int share_pkt_getdest(const pkthdr_t *ph, ftnaddr_t *fa)
{
	if ( I2H16( ph->phType ) != 2 )
		return 0;

	memset( fa, 0, sizeof( *fa ));
	fa->f = I2H16( ph->phDNode );
	fa->n = I2H16( ph->phDNet );

	/* Type-2+ capability bit (phCaps low bit) — same idea as binkd. */
	if ( I2H16( ph->phCaps ) & 1 ) {
		fa->z = I2H16( ph->phDZone );
		fa->p = I2H16( ph->phDPoint );
	} else {
		fa->z = I2H16( ph->phQDZone );
		fa->p = 0;
	}
	return 1;
}


static void share_pkt_setdest(pkthdr_t *ph, const ftnaddr_t *fa, const char *pwd)
{
	ph->phDNode = H2I16( fa->f );
	ph->phDNet = H2I16( fa->n );
	ph->phDZone = H2I16( fa->z );
	ph->phDPoint = H2I16( fa->p );
	ph->phQDZone = H2I16( fa->z );
	memset( ph->phPass, 0, sizeof( ph->phPass ));
	if ( pwd && *pwd )
		memcpy( ph->phPass, pwd, MIN( strlen( pwd ), sizeof( ph->phPass )));
}


FILE *share_open_rewritten_pkt(const char *tosend, char **tmp_path, int *rewritten)
{
	FILE		*in, *out;
	pkthdr_t	ph;
	FTNADDR_T	( dest );
	const ftnaddr_t	*peer;
	char		tmp[MAX_PATH];
	char		buf[8192];
	size_t		n;
	char		*pwd;

	if ( rewritten )
		*rewritten = 0;
	if ( tmp_path )
		*tmp_path = NULL;

	if ( !tosend || !share_chains )
		return NULL;

	peer = share_session_peer();
	if ( !peer )
		return NULL;

	in = fopen( tosend, "rb" );
	if ( !in )
		return NULL;

	if ( fread( &ph, sizeof( ph ), 1, in ) != 1 ) {
		fclose( in );
		return NULL;
	}

	if ( !share_pkt_getdest( &ph, &dest ) || !share_is_shared( &dest )) {
		fclose( in );
		return NULL;
	}

	/* From here on we must rewrite; signal that to the caller. */
	if ( rewritten )
		*rewritten = 1;

	/* Dest is shared — rewrite to session peer + that link's pkt password. */
	pwd = findpwd( peer );
	share_pkt_setdest( &ph, peer, pwd );

	snprintf( tmp, sizeof( tmp ), "/tmp/qshare.%04lx.pkt", (long) getpid());
	out = fopen( tmp, "wb" );
	if ( !out ) {
		write_log( "share: can't create temp pkt %s: %s", tmp, strerror( errno ));
		fclose( in );
		return NULL;
	}

	if ( fwrite( &ph, sizeof( ph ), 1, out ) != 1 ) {
		write_log( "share: can't write temp pkt header" );
		fclose( in );
		fclose( out );
		lunlink( tmp );
		return NULL;
	}

	while (( n = fread( buf, 1, sizeof( buf ), in )) > 0 ) {
		if ( fwrite( buf, 1, n, out ) != n ) {
			write_log( "share: can't write temp pkt body" );
			fclose( in );
			fclose( out );
			lunlink( tmp );
			return NULL;
		}
	}
	fclose( in );
	fclose( out );

	out = fopen( tmp, "rb" );
	if ( !out ) {
		lunlink( tmp );
		return NULL;
	}

	{
		char	from[40], to[40];

		xstrcpy( from, ftnaddrtoa( &dest ), sizeof( from ));
		xstrcpy( to, ftnaddrtoa( peer ), sizeof( to ));
		write_log( "share: rewrite %s dest %s -> %s",
			qbasename( tosend ), from, to );
	}

	if ( tmp_path )
		*tmp_path = xstrdup( tmp );
	return out;
}
