/***************************************************************************
 * Replace functions declarations
 * $Id: replace.h,v 1.5 2005/05/17 18:17:42 mitry Exp $
 ***************************************************************************/
#ifndef __REPLACE_H__
#define __REPLACE_H__

#ifndef WITH_PERL
#include <config.h>
#endif

#ifndef HAVE_STRSEP
extern char *strsep(char **str, const char *delim);
#endif

/*
 * glibc provides strcasestr but declares it only when a feature macro such
 * as _GNU_SOURCE is set. AC_CHECK_FUNCS still finds the symbol, so
 * HAVE_STRCASESTR is 1 and the old guard left callers with an implicit int
 * return. Declare it whenever this header is used. A system header that
 * already published the same type may declare it again; that is allowed.
 * When the libc has no strcasestr, strcasestr.c supplies this signature.
 */
extern char *strcasestr(const char *s, const char *find);

#ifndef HAVE_STRCSPN
extern size_t strcspn(const char *s1, register const char *s2);
#endif

#ifndef HAVE_GETSID
extern pid_t getsid(pid_t pid);
#endif

#ifndef HAVE_VSNPRINTF
#define vsnprintf(C,S,F,A...)	vsprintf((C),(F),##A)
#endif

#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

#ifndef WEXITSTATUS
#define WEXITSTATUS(stat_val) ((unsigned)(stat_val)>>8)
#endif

#endif
