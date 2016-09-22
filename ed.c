/*
Copyright (c) 2016, Carsten Kunze <carsten.kunze@arcor.de>

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.
*/

#include "compat.h"

#ifdef HAVE_CURSES_WCH
# include <wctype.h>
#endif

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "ui.h"
#include "main.h"
#include "exec.h"

#define LINESIZ (sizeof rbuf)

struct hist_ent {
	char *line;
	struct hist_ent *next, *prev;
};

static void init_edit(void);
static int edit_line(void (*)(char *));
static void linebuf_delch(void);
static void linebuf_insch(unsigned);
static void overful_del(void);
static void hist_add(void);
static void hist_up(void);
static void hist_down(void);

short edit;
unsigned histsize = 100;

static unsigned linelen, linepos, leftpos;
static struct hist_ent *history, *hist_ent, *hist_end;
static short have_hist_ent;
static unsigned histlen;

#ifdef HAVE_CURSES_WCH
wchar_t *linebuf;
static wchar_t ws[2];
static cchar_t cc;
#else
char *linebuf;
#endif

#ifdef NCURSES_MOUSE_VERSION
static void proc_mevent(void);

static MEVENT mevent;
#endif

void
ed_append(char *txt)
{
	size_t l;

	if (!edit)
		init_edit();

	l = strlen(txt);

	if (linelen + l >= LINESIZ) {
		printerr(NULL, "Line buffer overflow");
		return;
	}

#ifdef HAVE_CURSES_WCH
	mbstowcs(linebuf + linelen, txt, l + 1);
#else
	memcpy(linebuf + linelen, txt, l + 1);
#endif
	linelen += l;
	linepos = linelen;
}

static void
init_edit(void)
{
	edit = 1;
#ifdef HAVE_CURSES_WCH
	linebuf = malloc(LINESIZ * sizeof(*linebuf));
#else
	linebuf = rbuf;
#endif
	*linebuf = 0;
	linelen = linepos = leftpos = 0;
	*lbuf = 0;
	curs_set(1);
}

void
clr_edit(void)
{
	edit = 0;
#ifdef HAVE_CURSES_WCH
	free(linebuf);
#endif
	curs_set(0);
	werase(wstat);
	wrefresh(wstat);
}

void
disp_edit(void)
{
	werase(wstat);
	mvwaddstr(wstat, 0, 0, lbuf);
#ifdef HAVE_CURSES_WCH
	mvwaddwstr(wstat, 1, 0, linebuf + leftpos);
#else
	mvwaddstr(wstat, 1, 0, linebuf + leftpos);
#endif
	wmove(wstat, 1, linepos - leftpos);
	wrefresh(wstat);
}

void
set_fkey(int i, char *s)
{
#ifdef HAVE_CURSES_WCH
	size_t l;
#endif

	if (i < 1 || i > 10) {
		printf("Function key number must be in range 1-10\n");
		exit(1);
	}

	i--; /* key 1-10, storage 0-9 */
	free(sh_str[i]);
#ifdef HAVE_CURSES_WCH
	/* wcslen(wcs) should be <= strlen(mbs) */
	l = strlen(s) + 1;
	sh_str[i] = malloc(l * sizeof(wchar_t));
	mbstowcs(sh_str[i], s, l);
	free(s);
#else
	sh_str[i] = s;
#endif
}

int
ed_dialog(char *msg,
    /* NULL: leave buffer as-is */
    char *ini, void (*callback)(char *), int keep_buf, int use_hist)
{
	if (!edit)
		init_edit(); /* conditional, else rbuf is cleared! */

	memcpy(lbuf, msg, strlen(msg) + 1);

	if (ini) {
		linelen = 0; /* remove any existing text */
		ed_append(ini);
	}

	disp_edit();

	if (edit_line(callback)) {
		clr_edit();
		return 1;
	}

#ifdef HAVE_CURSES_WCH
	wcstombs(rbuf, linebuf, sizeof rbuf);
#endif
	if (!keep_buf)
		clr_edit();

	if (use_hist) {
		if (have_hist_ent) {
			have_hist_ent = 0;
			free(history->line);
			history->line = strdup(rbuf);
		} else
			hist_add();
	}

	return 0;
}

static void
hist_add(void)
{
	struct hist_ent *p;

	if (histsize < 2)
		return;

	if (histlen < histsize)
		histlen++;
	else {
		if ((p = hist_end->prev))
			p->next = NULL;

		free(hist_end);
		hist_end = p;
	}

	p = malloc(sizeof(struct hist_ent));
	p->line = strdup(rbuf);
	p->prev = NULL;

	if ((p->next = history))
		history->prev = p;
	else
		hist_end = p;

	history = p;
}

static int
edit_line(void (*callback)(char *))
{
#ifdef HAVE_CURSES_WCH
	wint_t c;
#else
	int c;
#endif
	int i;
	size_t l;

	while (1) {
next_key:
#ifdef HAVE_CURSES_WCH
		get_wch(&c);
#else
		c = getch();
#endif

		for (i = 0; i < (ssize_t)(sizeof(sh_str)/sizeof(*sh_str));
		    i++) {
			if (c !=
#ifdef HAVE_CURSES_WCH
			    (wint_t)
#endif
			    KEY_F(i + 1))
				continue;

			if (!sh_str[i])
				goto next_key;
#ifdef HAVE_CURSES_WCH
			l = wcslen(sh_str[i]);
#else
			l = strlen(sh_str[i]);
#endif
			if (linelen + l >= LINESIZ) {
				printerr(NULL, "Line buffer overflow");
				goto next_key;
			}

			linebuf_insch(l);
#ifdef HAVE_CURSES_WCH
			wmemcpy(linebuf + linepos, sh_str[i], l);
#else
			memcpy(linebuf + linepos, sh_str[i], l);
#endif
			linepos += l;
			disp_edit();
			goto next_key;
		}

		switch (c) {
#ifdef NCURSES_MOUSE_VERSION
		case KEY_MOUSE:
			proc_mevent();
			break;
#endif
		case 27:
			return 1;
		case '\n':
			return 0;
		case KEY_HOME:
			if (!linepos)
				break;

			linepos = 0;
			wmove(wstat, 1, 0);
			wrefresh(wstat);
			break;
		case KEY_LEFT:
			if (!linepos)
				break;

			wmove(wstat, 1, --linepos - leftpos);
			wrefresh(wstat);
			break;
		case KEY_END:
			if (linepos == linelen)
				break;

			linepos = linelen;
			wmove(wstat, 1, linepos - leftpos);
			wrefresh(wstat);
			break;
		case KEY_RIGHT:
			if (linepos == linelen)
				break;

			wmove(wstat, 1, ++linepos - leftpos);
			wrefresh(wstat);
			break;
		case KEY_BACKSPACE:
			if (!linepos)
				break;

			linepos--;
			wmove(wstat, 1, linepos - leftpos);
			goto del_char;
		case KEY_DC:
			if (linepos == linelen)
				break;

del_char:
			linebuf_delch();
			wdelch(wstat);

			if (leftpos)
				overful_del();

			wrefresh(wstat);

			if (callback) {
#ifdef HAVE_CURSES_WCH
				wcstombs(rbuf, linebuf, sizeof rbuf);
#endif
				callback(rbuf);
			}

			break;
		case KEY_UP:
			if (history)
				hist_up();
			break;
		case KEY_DOWN:
			if (have_hist_ent) /* else there had not been an up */
				hist_down();
			break;
		default:
			if (linelen + 1 >= LINESIZ)
				break;

			if (linelen >= statw - 1) {
				wmove(wstat, 1, 0);
				wdelch(wstat);
				wmove(wstat, 1, linepos - ++leftpos);
			}

			if (linepos == linelen) {
				linelen++;
				linebuf[linepos + 1] = 0;
			} else
				linebuf_insch(1);

			linebuf[linepos++] = c;

#ifdef HAVE_CURSES_WCH
			*ws = c;
			setcchar(&cc, ws, 0, 0, NULL);
#endif

			if (linepos == linelen) {
#ifdef HAVE_CURSES_WCH
				wadd_wch(wstat, &cc);
#else
				waddch(wstat, c);
#endif
			} else {
#ifdef HAVE_CURSES_WCH
				wins_wch(wstat, &cc);
#else
				winsch(wstat, c);
#endif
				wmove(wstat, 1, linepos - leftpos);
			}

			wrefresh(wstat);

			if (callback) {
#ifdef HAVE_CURSES_WCH
				wcstombs(rbuf, linebuf, sizeof rbuf);
#endif
				callback(rbuf);
			}
		}
	}
}

static void
overful_del(void)
{
	wmove(wstat, 1, 0);
#ifdef HAVE_CURSES_WCH
	*ws = linebuf[--leftpos];
	setcchar(&cc, ws, 0, 0, NULL);
	wins_wch(wstat, &cc);
#else
	winsch(wstat, linebuf[leftpos]);
#endif
	wmove(wstat, 1, linepos - leftpos);
}

static void
linebuf_delch(void)
{
	unsigned i, j;

	for (i = j = linepos; i < linelen; i = j)
		linebuf[i] = linebuf[++j];

	linelen--;
}

static void
linebuf_insch(unsigned num)
{
	int i, j;

	linelen += num;

	for (j = linelen, i = j - num; i >= (int)linepos; i--, j--)
		linebuf[j] = linebuf[i];
}

#ifdef NCURSES_MOUSE_VERSION
static void
proc_mevent(void)
{
	if (getmouse(&mevent) != OK)
		return;

	if (mevent.bstate & BUTTON1_CLICKED ||
	    mevent.bstate & BUTTON1_DOUBLE_CLICKED ||
	    mevent.bstate & BUTTON1_PRESSED) {
		if (mevent.y != LINES - 1)
			return;

		linepos = leftpos + mevent.x;
		wmove(wstat, 1, linepos - leftpos);
		wrefresh(wstat);
	}
}
#endif

static void
hist_up(void)
{
	if (!histsize || (have_hist_ent && !hist_ent->next))
		return;

#ifdef HAVE_CURSES_WCH
	if (!have_hist_ent || !hist_ent->prev)
		wcstombs(rbuf, linebuf, sizeof rbuf);
#endif

	if (!have_hist_ent) {
		have_hist_ent = 1;
		hist_add();
		hist_ent = history;
	} else if (!hist_ent->prev) {
		free(history->line);
		history->line = strdup(rbuf);
	}

	hist_ent = hist_ent->next;
	*linebuf = 0;
	linelen = 0;
	ed_append(hist_ent->line);
	disp_edit();
}

static void
hist_down(void)
{
	if (!histsize || !hist_ent->prev)
		return;

	hist_ent = hist_ent->prev;
	*linebuf = 0;
	linelen = 0;
	ed_append(hist_ent->line);
	disp_edit();
}