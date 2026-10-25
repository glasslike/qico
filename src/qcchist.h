/**********************************************************
 * qcc session history. --disable-qcc-binlog leaves it out of qcc.
 *
 * The view reads the binlog file itself. It is not a daemon
 * command and it is not a line slot.
 **********************************************************/
#ifndef __QCCHIST_H__
#define __QCCHIST_H__

#include <time.h>

#ifdef HAVE_NCURSES_H
#include <ncurses.h>
#else
#include <curses.h>
#endif

void		hist_set_path(const char *path);
int		hist_on(void);
const char	*hist_title(void);
const char	*hist_status(void);

/*
 * Open or close the view. mh is the upper-window height qcc is using.
 * NULL means the view changed. A returned string is a reason to stay
 * on the queue; the caller prints it and beeps.
 */
const char	*hist_toggle(int mh);
void		hist_resize(int mh);

/* 1 when a new frame or a new local day changed the list. */
int		hist_poll(time_t now);

/* Queue-style motion. body is the number of session rows on screen. */
void		hist_key(int ch, int body);

void		hist_draw_card(WINDOW *wmain);
void		hist_draw_list(void);

/* Drop the list window so a line log can use that pane. Records stay. */
void		hist_hide_list(void);

#endif
