/*
 * window.c	Very portable window routines.
 *		Currently this code is used in _both_ the BBS
 *		system and minicom.
 *
 * Author:	Miquel van Smoorenburg, <miquels@drinkel.nl.mugnet.org>
 * 
 */

#include <sys/types.h>
#if defined(_MINIX) || defined(linux)
#  include <termcap.h>
#else
char *tgetstr(), *tgoto();
int tgetent(), tputs(), tgetnum(), tgetflag();
#endif
#include <string.h>
#if defined (_POSIX_SOURCE) || defined(_BSD43)
#  include <stdlib.h>
#  include <unistd.h>
#else
  char *getenv();
#endif
#include "charmap.h"

/* Don't want to include all header stuff for three prototypes from sysdep.c */
#if __STDC__
  int setcbreak(int);
  int getch(void);
  void getrowcols(int *rows, int *cols);
#else
  int setcbreak();
  int getch();
  void getrowcols();
#endif

#if 0 /* Should use it in wprintf */
#ifdef _MINIX
#  include <stdarg.h>
#else
#  include <varargs.h>
#endif
#endif

#include <stdio.h>
#include <signal.h>
#include "window.h"
#ifndef BBS
#include "config.h"
#endif

#define BUFFERSIZE 2048

#define swap(x, y) { int d = (x); (x) = (y); (y) = d; }

/* Terminal capabilities */
static char *CM, *IS, *RS;
static char *ME, *SE, *UE, *AE;
static char *AS, *MB, *MD, *MR, *SO, *US;
static char *CE, *Al, *Dl, *AL, *DL;
static char *CS, *SF, *SR, *VB;
static char *VE, *VI, *KS, *KE;
static char *CD, *CL, *IC, *DC;
static char *BC, *CR, *NL;

/* Special characters */
static char D_UL;
static char D_HOR;
static char D_UR;
static char D_LL;
static char D_VER;
static char D_LR;

static char S_UL;
static char S_HOR;
static char S_UR;
static char S_LL;
static char S_VER;
static char S_LR;

static char _bufstart[BUFFERSIZE];
static char *_bufpos = _bufstart;
static char *_buffend;
static ELM *gmap;

static char curattr = -1;
static char curcolor = -1;
static int curx = -1;
static int cury = -1;
static int _intern = 0;
static int _curstype = CNORMAL;
static int _has_am = 0;
static int _mv_standout = 0;
static ELM oldc;
static int sflag = 0;

/*
 * Smooth is only defined for slow machines running Minicom.
 * With this defined, Minicom will buffer only per-line
 * and the output will look much less 'jerky'. (I hope :-)
 */
#ifdef SMOOTH
static WIN *curwin = NIL_WIN;
extern WIN *us;
#endif

int useattr = 1;
int dirflush = 1;
int LINES, COLS;
int usecolor = 0;
WIN *stdwin;
char *_tptr = CNULL;
int literal = 0;
int w_init = 0;

#if DEBUG

/*
 * Debug to stdout
 */
int debug(s, a1, a2, a3, a4)
char *s;
int a1, a2, a3, a4;
{
  char lala[80];

  sprintf(lala, s, a1, a2, a3, a4);
  write(2, lala, strlen(lala));
  return(0);
}
#endif

/* ===== Low level routines ===== */

/*
 * Flush the screen buffer
 */
void wflush()
{
  register int todo, done;

  todo = _bufpos - _bufstart;
  _bufpos = _bufstart;

  while(todo > 0) {
  	done = write(1, _bufpos, todo);
  	if (done > 0) {
  		todo -= done;
  		_bufpos += done;
  	}
  }
  _bufpos = _bufstart;
}

/*
 * Output a raw character to the screen
 */
static int outchar(c)
int c;
{
  *_bufpos++ = c;
  if (_bufpos >= _buffend) wflush();
#if defined(SMOOTH)
  if (curwin == us && (c == '\n' || c == '\r')) wflush();
#endif
  return(0);
}

/*
 * Output a raw string to the screen.
 */
static void outstr(s)
char *s;
{
#ifdef _MINIX
  while(*s) (void) outchar (*s++);
#else
  tputs(s, 1, outchar);
#endif
}


/*
 * Turn off all attributes
 */
static void _attroff()
{
  if (ME != CNULL)
  	outstr(ME);
  else {
  	if (SE != CNULL) outstr(SE);
  	if (UE != CNULL) outstr(UE);
  }
  if (AE != CNULL) outstr(AE);
}

/*
 * Turn some attributes on
 */
static void _attron(attr)
char attr;
{
  if (!usecolor || (attr & A_REVERSE) == 0) {
	/* Reverse standout does not look too good.. */
	if (attr & A_BOLD	&& MD != CNULL)  outstr(MD);
  	if (attr & A_STANDOUT   && SO != CNULL)  outstr(SO);
	if (attr & A_UNDERLINE  && US != CNULL)  outstr(US);
  }
  if (attr & A_REVERSE	  && MR != CNULL)  outstr(MR);
  if (attr & A_ALTCHARSET && AS != CNULL)  outstr(AS);
  if (attr & A_BLINK      && MB != CNULL)  outstr(MB);
}

/*
 * Set the colors
 */
static void _colson(color)
char color;
{
  char buf[12];
  sprintf(buf, "\033[%d;%dm", COLFG(color) + 30, COLBG(color) + 40);
  outstr(buf);
}
  
/*
 * Set global attributes, if different.
 */
static void _setattr(attr, color)
char attr, color;
{
  if (!useattr) return;

  if (!usecolor) {
  	curcolor = color;
  	if (attr == curattr) return;
  	curattr = attr;
  	_attroff();
  	_attron(attr);
  	return;
  }
  if (attr == curattr && color == curcolor) return;
  _attroff();
  _colson(color);
  _attron(attr);
  curattr = attr;
  curcolor = color;
}

/*
 * Goto (x, y) in stdwin
 */
static void _gotoxy(x, y)
int x, y;
{
  register oldattr = -1;

  if (x < COLS && y < LINES && (x != curx || y != cury)) {
  	if (!_mv_standout && curattr != A_NORMAL) {
  		oldattr = curattr;
  		_setattr(A_NORMAL, curcolor);
  	}
  	if (CR != CNULL && y == cury && x == 0)
  		outstr(CR);
#if 0 /* Hmm, sometimes NL only works in the first column */
	else if (NL != CNULL && x == curx && y == cury + 1)
		outstr(NL);
#else
	else if (NL != CNULL && x == 0 && x == curx && y == cury + 1)
		outstr(NL);
#endif
  	else if (BC != CNULL && y == cury && x == curx - 1)
  		outstr(BC);
  	else	
  		outstr(tgoto(CM, x, y));
  	curx = x;
  	cury = y;
  	if (oldattr != -1) _setattr(oldattr, curcolor);
  } 
}

/*
 * Write a character in stdwin at x, y with attr & color
 * 'doit' can be  -1: only write to screen, not to memory
 *                 0: only write to memory, not to screen
 *                 1: write to both screen and memory
 */
static void _write(c, doit, x, y,attr, color)
int c, doit;
int x, y;
char attr, color;
{
  register ELM *e;

  /* If the terminal has automatic margins, we can't write to the
   * last line, last character. After scrolling, this "invisible"
   * character is automatically restored.
   */
  if (_has_am && y >= LINES - 1 && x >= COLS - 1) {
  	doit = 0;
  	sflag = 1;
  	oldc.value = c;
  	oldc.attr = attr;
  	oldc.color = color;
  }
  if (x < COLS && y < LINES) {
     if (doit != 0) {
  	_gotoxy(x, y);
  	_setattr(attr, color);
#ifdef _ACK
	c &= 0xFF;
#endif
	(void) outchar(literal ? c : wcharmap[(unsigned char)c]);
	curx++;
     }
     if (doit >= 0) {
	e = &gmap[x + y * COLS];
	e->value = c;
	e->attr = attr;
	e->color = color;
     }
  }
}

/*
 * Set cursor type.
 */
static void _cursor(type)
int type;
{
  _curstype = type;

  if (type == CNORMAL && VE != CNULL) outstr(VE);
  if (type == CNONE && VE != CNULL && VI != CNULL) outstr(VI);
}


/* ==== High level routines ==== */


#if 0
/* This code is functional, but not yet used.
 * It might be one day....
 */
/*
 * Resize a window
 */
void wresize(win, lines, cols)
WIN *win;
int lines, cols;
{
  int x, y;
  ELM *oldmap, *newmap, *e, *n;

  if ((newmap = (ELM *)malloc(lines * cols * sizeof(ELM))) == (ELM *)NULL)
	return;
  if (win == stdwin)
	oldmap = gmap;
  else
	oldmap = win->map;

  for(y = 0; y < lines; y++)
	for(x = 0; x < cols; x++) {
		n = &newmap[y + x * cols];
		if (x < win->xs && y < win->ys) {
			e = &oldmap[y + x * COLS];
			n->value = e->value;
			n->color = e->color;
			n->attr = e->attr;
		} else {
			n->value = ' ';
			n->color = win->color;
			n->attr = win->attr;
		}
	}
  if (win->sy2 == win->y2) win->sy2 = win->y1 + lines - 1;
  win->y2 = win->y1 + lines - 1;
  win->ys = lines;
  win->xs = cols;
  free(oldmap);
  if (win == stdwin) {
	gmap = newmap;
	LINES = lines;
	COLS = cols;
  } else
	win->map = newmap;
}
#endif

/*
 * Create a new window.
 */
/*ARGSUSED*/
WIN *wopen(x1, y1, x2, y2, border, attr, fg, bg, direct, histlines, rel)
int x1, y1, x2, y2;
int border;
int attr, fg, bg, direct;
int histlines;
int rel;
{
  WIN *w;
  ELM *e;
  int bytes;
  int x, y;
  int color;
  int offs;
  int doclr = 1;		/* Could later be added as argument to func */
#ifdef SMOOTH
  curwin = NIL_WIN;
#endif

  if ((w = (WIN *)malloc(sizeof(WIN))) == (WIN *)0) return(w);
  
  offs = (border != BNONE);

  if (x1 < offs) x1 = offs;
  if (y1 < offs) y1 = offs;
#if 0
  if (x2 >= COLS - offs) x2 = COLS - offs - 1;
  if (y2 >= LINES - offs) y2 = LINES - offs - 1;
#endif
  if (x1 > x2) swap(x1, x2);
  if (y1 > y2) swap(y1, y2);

  w->xs = x2 - x1 + 1;
  w->ys = y2 - y1 + 1;
  w->x1 = x1;
  w->x2 = x2;
  w->y1 = w->sy1 = y1;
  w->y2 = w->sy2 = y2;
  w->doscroll = 1;
  w->border = border;
  w->cursor = CNORMAL;
  w->attr = attr;
  w->autocr = 1;
  w->wrap = 1;
  color = w->color = COLATTR(fg, bg);
  w->curx = 0;
  w->cury = 0;

  w->o_curx = curx;
  w->o_cury = cury;
  w->o_attr = curattr;
  w->o_color = curcolor;
  w->o_cursor = _curstype;
  w->direct = direct;

  if (border != BNONE) {
  	x1--; x2++;
  	y1--; y2++;
  }
  /* Store whatever we are overlapping */
  bytes = (y2 - y1 + 1) * (x2 - x1 + 1) * sizeof(ELM) + 100;
  if ((e = (ELM *)malloc(bytes)) == (ELM *)0) {
  	free(w);
  	return((WIN *)0);
  }
  w->map = e;
  /* How many bytes is one line */
  bytes = (x2 - x1 + 1) * sizeof(ELM);
  /* Loop */
  for(y = y1; y <= y2; y++) {
  	memcpy(e, gmap + COLS * y + x1, bytes);
  	e += (x2 - x1 + 1);
  }
  
#if HISTORY
  /* Do we want history? */
  w->histline = w->histlines = 0;
  w->histbuf = (ELM *)0;
  if (histlines) {
	/* Reserve some memory. */
	bytes = w->xs * histlines * sizeof(ELM);
	if ((w->histbuf = (ELM *)malloc(bytes)) == NULL) {
		free(w->map);
		free(w);
		return((WIN *)0);
	}
	w->histlines = histlines;

	/* Clear the history buf. */
	e = w->histbuf;
	for(y = 0; y < w->xs * histlines; y++) {
		e->value = ' ';
		e->attr = attr;
		e->color = color;
		e++;
	}
  }
#endif

  /* And draw the window */
  if (border) {
	_write(border == BSINGLE ? S_UL : D_UL, w->direct, x1, y1,
					attr, color);
	for(x = x1 + 1; x < x2; x++)
		_write(border == BSINGLE ? S_HOR : D_HOR, w->direct, x, y1,
					attr, color);
	_write(border == BSINGLE ? S_UR : D_UR, w->direct, x2, y1,
					attr, color);
	for(y = y1 + 1; y < y2; y++) {
		_write(border == BSINGLE ? S_VER : D_VER, w->direct, x1, y,
					attr, color);
		for(x = x1 + 1; x < x2; x++)
			_write(' ', w->direct, x, y, attr, color);
		_write(border == BSINGLE ? S_VER : D_VER, w->direct, x2, y,
					attr, color);
	}
	_write(border == BSINGLE ? S_LL : D_LL, w->direct, x1, y2,
					attr, color);
	for(x = x1 + 1; x < x2; x++)
		_write(border == BSINGLE ? S_HOR : D_HOR, w->direct,
					x, y2, attr, color);
	_write(border == BSINGLE ? S_LR : D_LR, w->direct, x2, y2,
					attr, color);
	if (w->direct) _gotoxy(x1 + 1, y1 + 1);
  } else
  	if (doclr) winclr(w);
  wcursor(w, CNORMAL);	

  if (w->direct) wflush();
  return(w);
}

/*
 * Close a window.
 */
void wclose(win, replace)
WIN *win;
int replace;
{
  register ELM *e;
  register int x, y;

#ifdef SMOOTH
  curwin = NIL_WIN;
#endif

  if (win == stdwin) {
  	win_end();
  	return;
  }
  e = win->map;

  if (win->border) {
  	win->x1--; win->x2++;
  	win->y1--; win->y2++;
  }
  wcursor(win, win->o_cursor);
  if (replace) {
	for(y = win->y1; y <= win->y2; y++) {
  		for(x = win->x1; x <= win->x2; x++) {
  			_write(e->value, 1, x, y, e->attr, e->color);
  			e++;
  		}
 	}
	_gotoxy(win->o_curx, win->o_cury);
	_setattr(win->o_attr, win->o_color);
  }
  free(win->map);
  free(win);
#if HISTORY
  if (win->histbuf) free(win->histbuf);
#endif
  wflush();
}

static int oldx, oldy;
static int ocursor;

/*
 * Clear screen & restore keyboard modes
 */
void wleave()
{
  oldx = curx;
  oldy = cury;
  ocursor = _curstype;

  (void) setcbreak(0); /* Normal */
  _gotoxy(0, LINES - 1);
  _setattr(A_NORMAL, COLATTR(WHITE, BLACK));
  _cursor(CNORMAL);
  if (CL != CNULL)
	outstr(CL);
  else
	outstr("\n");
  if (KE != CNULL) outstr(KE);
  if (RS != CNULL) outstr(RS);
  wflush();
}

void wreturn()
{
  int x, y;
  ELM *e;

#ifdef SMOOTH
  curwin = NIL_WIN;
#endif

  curattr = -1;
  curcolor = -1;

  (void) setcbreak(1); /* Cbreak, no echo */

  if (KS != CNULL) outstr(KS); /* Keypad mode */
  if (IS != CNULL) outstr(IS); /* Initialization string */
  
  _gotoxy(0, 0);
  _cursor(ocursor);

  e = gmap;
  for(y = 0; y <LINES; y++) {
  	for(x = 0; x < COLS; x++) {
  		_write(e->value, -1, x, y, e->attr, e->color);
  		e++;
  	}
  }
  _gotoxy(oldx, oldy);
  wflush();
}

/*
 * Redraw the whole window.
 */
void wredraw(w, newdirect)
WIN *w;
int newdirect;
{
  int minx, maxx, miny, maxy;
  register ELM *e;
  register int x, y;
  int addcnt;

  minx = w->x1;
  maxx = w->x2;
  miny = w->y1;
  maxy = w->y2;
  addcnt = stdwin->xs - w->xs;

  if (w->border) {
  	minx--;
  	maxx++;
  	miny--;
  	maxy++;
  	addcnt -= 2;
  }

  _cursor(CNONE);
  e = gmap + (miny * stdwin->xs) + minx;

  for(y = miny; y <= maxy; y++) {
  	for(x = minx; x <= maxx; x++) {
  		_write(e->value, -1, x, y, e->attr, e->color);
  		e++;
  	}
  	e += addcnt;
  }
  _cursor(w->cursor);
  _gotoxy(w->x1 + w->curx, w->y1 + w->cury);
  wflush();
  w->direct = newdirect;
}

/*
 * Clear to end of line, low level.
 */
static int _wclreol(w)
WIN *w;
{
  register x;
  register doit = 1;
  register y;
  
#ifdef SMOOTH
  curwin = w;
#endif
  y = w->cury + w->y1;

  if (w->direct && (w->x2 == COLS - 1) && CE) {
  	_gotoxy(w->curx + w->x1, y);
  	_setattr(w->attr, w->color);
  	outstr(CE);
  	doit = 0;
  }
  for(x = w->curx + w->x1; x <= w->x2; x++)
  	_write(' ', w->direct && doit, x, y, w->attr, w->color);
  return(doit);	
}

/*
 * Scroll a window.
 */
void wscroll(win, dir)
WIN *win;
int dir;
{
  register ELM *e, *f;
  register char *src, *dst;
  register int x, y;
  register int doit = 1;
  int ocurx, fs = 0, len;
  int phys_scr = 0;

#ifdef SMOOTH
  curwin = win;
#endif

  /*
   * If the window *is* the physical screen, we can scroll very simple.
   * This improves performance on slow screens (eg ATARI ST) dramatically.
   */
  if (win->direct && SF != CNULL &&
  	(dir == S_UP || SR != CNULL) && (LINES == win->sy2 - win->sy1 + 1)) {
  	doit = 0;
  	phys_scr = 1;
  	_setattr(win->attr, win->color);
  	if (dir == S_UP) {
  		_gotoxy(0, LINES - 1);
  		outstr(SF);
  	} else {
  		_gotoxy(0, 0);
  		outstr(SR);
  	}
  }
  /*
   * If the window is as wide as the physical screen, we can
   * scroll it with insert/delete line (or set scroll region - vt100!)
   */
  else if (win->direct && win->xs == COLS &&
  		((CS != CNULL && SF != CNULL && SR != CNULL)
  		|| (Dl != CNULL && Al != CNULL))) {
  	doit = 0;
  	phys_scr = 1;
  	_setattr(win->attr, win->color);
  	if (CS != CNULL && SF != CNULL && SR != CNULL) { /* Scrolling Region */
		/* If the scroll region we want to initialize already is as
		 * big as the physical screen, we don't _have_ to
		 * initialize it.
		 */
  		if (win->sy2 == LINES - 1 && win->sy1 == 0) fs = 1;
  		if (!fs) {
  			outstr(tgoto(CS, win->sy2, win->sy1));
  			cury = 0;
  		}	
  		if (dir == S_UP) {
  		 	_gotoxy(0, win->sy2);
  		 	outstr(SF);
  		} else {
  		 	_gotoxy(0, win->sy1);
  		 	outstr(SR);
  		}
  		if (!fs) {
  			outstr(tgoto(CS, LINES - 1, 0));
  			cury = 0;
  		}	
  		_gotoxy(0, win->sy2);
  	} else { /* Use insert/delete line */
  		if (dir == S_UP) {
  			_gotoxy(0, win->sy1);
  			outstr(Dl);
  			_gotoxy(0, win->sy2);
  			outstr(Al);
  		} else {
  			_gotoxy(0, win->sy2);
  			outstr(Dl);
  			_gotoxy(0, win->sy1);
  			outstr(Al);
  		}
  	}
  }

  /* If a terminal has automatic margins, we can't write
   * to the lower right. After scrolling we have to restore
   * the non-visible character that is now visible.
   */
  if (sflag && win->sy2 == (LINES - 1) && win->sy1 != win->sy2) {
  	if (dir == S_UP) {
  		_write(oldc.value, 1, COLS - 1, LINES - 2,
  			oldc.attr, oldc.color);
  	}
  	sflag = 0;
  }

  ocurx = win->curx;

#if HISTORY
  /* If this window has a history buf, see if we want to use it. */
  if (win->histbuf && dir == S_UP &&
	win->sy2 == win->y2 && win->sy1 == win->y1) {

	/* Calculate screen buffer */
  	e = gmap + win->y1 * COLS + win->x1;

	/* Calculate history buffer */
	f = win->histbuf + (win->xs * win->histline);

	/* Copy line from screen to history buffer */
	memcpy((char *)f, (char *)e, win->xs * sizeof(ELM));

	/* Postion the next line in the history buffer */
	win->histline++;
	if (win->histline >= win->histlines) win->histline = 0;
  }
#endif

  /* If the window is screen-wide and has no border, there
   * is a much simpler & FASTER way of scrolling the memory image !!
   */
  if (phys_scr) {
  	len = (win->sy2 - win->sy1) * win->xs * sizeof(ELM);
  	if (dir == S_UP)  {
  		dst = (char *)&gmap[0];				/* First line */
  		src = (char *)&gmap[win->xs];			/* Second line */
  		win->cury = win->sy2 - win->y1;
  	} else {
  		src = (char *)&gmap[0];				/* First line */
  		dst = (char *)&gmap[win->xs];			/* Second line */
  		win->cury = win->sy1 - win->y1;
  	}
  	/* memmove copies len bytes from src to dst, even if the
  	 * objects overlap.
  	 */
  	fflush(stdout);
#ifdef _SYSV
  	memcpy((char *)dst, (char *)src, len);
#else
  	memmove((char *)dst, (char *)src, len);
#endif
  } else {
	/* Now scroll the memory image. */
  	if (dir == S_UP) {
  		for(y = win->sy1 + 1; y <= win->sy2; y++) {
  			e = gmap + y * COLS + win->x1;
  			for(x = win->x1; x <= win->x2; x++) {
  			   _write(e->value, win->direct && doit,
  			   		x, y - 1, e->attr, e->color);
  			   e++;
 	 		}
  		}
  		win->curx = 0;
  		win->cury = win->sy2 - win->y1;
  		if (doit) (void) _wclreol(win);
  	} else {
  		for(y = win->sy2 - 1; y >= win->sy1; y--) {
  			e = gmap + y * COLS + win->x1;
  			for(x = win->x1; x <= win->x2; x++) {
  			   _write(e->value, win->direct && doit,
  			   		x, y + 1, e->attr, e->color);
  			   e++;
  			}
  		}
  		win->curx = 0;
  		win->cury = win->sy1 - win->y1;
  		if (doit) (void) _wclreol(win);
  	}
  }

  win->curx = ocurx;

  if (!doit) for(x = win->x1; x <= win->x2; x++)
		_write(' ', 0, x, win->y1 + win->cury, win->attr, win->color);
  if (!_intern && win->direct)
  	_gotoxy(win->x1 + win->curx, win->y1 + win->cury);
  if (dirflush && !_intern && win->direct) wflush();
}

/*
 * Locate the cursor in a window.
 */
void wlocate(win, x, y)
WIN *win;
int x, y;
{
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x >= win->xs) x = win->xs - 1;
  if (y >= win->ys) y = win->ys - 1;

  win->curx = x;
  win->cury = y;
  if (win->direct) _gotoxy(win->x1 + x, win->y1 + y);

  if (dirflush) wflush();
}

/*
 * Print a character in a window.
 */
void wputc(win, c)
WIN *win;
int c;
{
  register mv = 0;

#ifdef SMOOTH
  curwin = win;
#endif

  switch(c) {
  	case '\r':
  		win->curx = 0;
  		mv++;
  		break;
  	case '\n':
  		win->cury++;
  		if (win->autocr) win->curx = 0;
  		mv++;
  		break;
  	case '\b':
  		if (win->curx == 0) break;
  		win->curx--;
  		mv++;
  		break;
  	case '\007':
  		wbell();
  		break;
  	case '\t':
  		do {
  			wputc(win, ' '); /* Recursion! */
  		} while(win->curx % 8);
  		break;
  	default:
		_write(c, win->direct, win->curx + win->x1,
			win->cury + win->y1, win->attr, win->color);
  		if (++win->curx >= win->xs) {
  			if (win->wrap) {
  				win->curx = 0;
  				win->cury++;
  			} else {
  				win->curx--;	
  				curx = 0; /* Force to move */
  			}
  			mv++;
  		}
  		break;
  }
  if (mv) {
	if (win->cury == win->sy2 - win->y1 + 1) {
  		if (win->doscroll)
  			wscroll(win, S_UP);
  		else
  			win->cury = win->sy1 - win->y1;
	}
	if (win->cury >= win->ys) win->cury = win->ys - 1;
  	if (win->direct) _gotoxy(win->x1 + win->curx, win->y1 + win->cury);
  }
  if (win->direct && dirflush && !_intern) wflush();
}

/* Draw one line in a window */
void wdrawelm(w, y, e)
WIN *w;
int y;
ELM *e;
{
  int x;

  for(x = w->x1; x < w->x2; x++) {
	_write(e->value, w->direct, x,
		y + w->y1, e->attr, e->color);
	e++;
  }
}

/*
 * Print a string in a window.
 */
void wputs(win, s)
WIN *win;
register char *s;
{
  _intern = 1;

  while(*s) wputc(win, *s++);
  if (dirflush && win->direct) wflush();
  _intern = 0;
}

/*
 * Print a formatted string in a window.
 * Should return stringlength - but who cares.
 */
/*VARARGS1*/
int wprintf(win, s, a1, a2, a3, a4, a5, a6, a7, a8)
WIN *win;
char *s, *a1, *a2, *a3, *a4, *a5, *a6, *a7, *a8;
{
  char buf[128];
  char *t;

  t = buf;

  _intern = 1;
  sprintf(buf, s, a1, a2, a3, a4, a5, a6, a7, a8);
  while(*t) wputc(win, *t++);
  _intern = 0;
  if (dirflush && win->direct) wflush();

  return(0);
}

/*
 * Sound a bell.
 */
void wbell()
{
  if (VB != CNULL)
  	outstr(VB);
  else
  	(void) outchar('\007');
  wflush();
}

/*
 * Set cursor type.
 */
void wcursor(win, type)
WIN *win;
int type;
{
  win->cursor = type;
  if (win->direct) {
  	_cursor(type);
	if (dirflush) wflush();
  }
}

void wtitle(w, pos, s)
WIN *w;
int pos;
char *s;
{
  int x;

#ifdef SMOOTH
  curwin = NIL_WIN;
#endif

  if (w->border == BNONE) return;
  
  if (pos == TLEFT) x = w->x1;
  if (pos == TRIGHT) x = w->x2 - strlen(s) - 1;
  if (pos == TMID) x = w->x1 + (w->xs - strlen(s)) / 2 - 1;
  if (x < w->x1) x = w->x1;

  if (x < w->x2) _write('[', w->direct, x++, w->y1 - 1, w->attr, w->color);
  while(*s && x <= w->x2) _write(*s++, w->direct, x++, w->y1 - 1,
  		w->attr, w->color);
  if (x <= w->x2) _write(']', w->direct, x++, w->y1 - 1, w->attr, w->color);

  if (w->direct) {
  	_gotoxy(w->x1 + w->curx, w->y1 + w->cury);
	if (dirflush) wflush();
  }
}


/* ==== Menu Functions ==== */

/*
 * Change attributes of one line of a window.
 */
void wcurbar(w, y, attr)
WIN *w;
int y;
int attr;
{
  ELM *e;
  register x;

#ifdef SMOOTH
  curwin = w;
#endif

  y += w->y1;

  e = gmap + y * COLS + w->x1;
  
  /* If we can't do reverse, just put a '>' in front of
   * the line. We only support A_NORMAL & A_REVERSE.
   */
  if (!useattr || MR == CNULL) {
  	if (attr & A_REVERSE)
  		x = '>';
  	else
  		x = ' ';
  	_write(x, w->direct, w->x1, y, attr, e->color);
  } else {
	for(x = w->x1; x <= w->x2; x++) {
  		_write(e->value, w->direct, x, y, attr, e->color);
  		e++;
	}
  }
  if ((VI == CNULL || _curstype == CNORMAL) && w->direct)
  	_gotoxy(w->x1, y);
  if (w->direct) wflush();
}

/*
 * wselect - select one of many choices.
 */
int wselect(x, y, choices, funlist, title, attr, fg, bg)
int x, y;
char **choices;
void (**funlist)();
char *title;
int attr, fg, bg;
{
  char **a = choices;
  int len = 0;
  int count = 0;
  int cur = 0;
  int c;
  WIN *w;
  int high_on = A_REVERSE | attr;
  int high_off = attr;
  
  /* first count how many, and max. width. */

  while(*a != CNULL) {
  	count++;
  	if (strlen(*a) > len) len = strlen(*a);
  	a++;
  }
  if (title != CNULL && strlen(title) + 2 > len) len = strlen(title) + 2;
  if (attr & A_REVERSE) {
  	high_on = attr & ~A_REVERSE;
  	high_off = attr;
  }

  if ((w = wopen(x, y, x + len + 2, y + count - 1, BDOUBLE,
	attr, fg, bg, 0, 0, 0)) == (WIN *)0) return(-1);
  wcursor(w, CNONE);

  if (title != CNULL) wtitle(w, TMID, title);

  for(c = 0; c < count; c++) {
  	wprintf(w, " %s%s", choices[c], c == count - 1 ? "" : "\n");
  }

  wcurbar(w, cur, high_on);
  wredraw(w, 1);

  while (1) {
	while((c = getch()) != 27 && c != '\n' && c!= '\r' && c != ' ') {
		if (c == K_UP || c == K_DN || c == 'j' || c == 'k')
			wcurbar(w, cur, high_off);
		switch(c) {
			case K_UP:
			case 'k':
				cur--;
				if (cur < 0) cur = count - 1;
				break;
			case K_DN:
			case 'j':
				cur++;
				if (cur >= count) cur = 0;
				break;
		}
		if (c == K_UP || c == K_DN || c == 'j' || c == 'k')
			wcurbar(w, cur, high_on);
	}
	wcursor(w, CNORMAL);
	if (c == ' ' || c == 27) {
		wclose(w, 1);
		return(0);
	}
	if (funlist == NIL_FUNLIST || funlist[cur] == NIL_FUN) {
		wclose(w, 1);
		return(cur + 1);
	}
	(*funlist[cur])();
	wcursor(w, CNONE);
  }
}


/* ==== Clearing functions ==== */

/*
 * Clear entire line.
 */
void wclrel(w)
WIN *w;
{
  int ocurx = w->curx;
  
  w->curx = 0;
  (void) _wclreol(w);
  w->curx = ocurx;
  wlocate(w, ocurx, w->cury);
}

/*
 * Clear to end of line.
 */
void wclreol(w)
WIN *w;
{
  if (_wclreol(w) && w->direct) _gotoxy(w->x1 + w->curx, w->y1 + w->cury);
  if (dirflush) wflush();
}

/*
 * Clear to begin of line
 */
void wclrbol(w)
WIN *w;
{
  register x;
  int y;
  
#ifdef SMOOTH
  curwin = w;
#endif

  y = w->curx + w->y1;

  if (w->direct) _gotoxy(w->x1, y);
  
  for(x = w->x1; x <= w->x2; x++) _write(' ', w->direct, x, y,
  		w->attr, w->color);
  if (w->direct) {
  	_gotoxy(w->x1 + curx, y);
	if (dirflush) wflush();
  }
}

/*
 * Clear to end of screen
 */
void wclreos(w)
WIN *w;
{
  register y;
  int ocurx, ocury;
  
  ocurx = w->curx;
  ocury = w->cury;
  
  w->curx = 0;

  for(y = w->cury + 1; y <= w->y2 - w->y1; y++) {
  	w->cury = y;
  	(void) _wclreol(w);
  }
  w->curx = ocurx;
  w->cury = ocury;
  if (_wclreol(w) && w->direct) _gotoxy(w->x1 + w->curx, w->y1 + w->cury);
  if (dirflush && w->direct) wflush();
}

/*
 * Clear to begin of screen.
 */
void wclrbos(w)
WIN *w;
{
  int ocurx, ocury;
  register y;
  
  ocurx = w->curx;
  ocury = w->cury;
  
  w->curx = 0;
  
  for(y = 0; y < ocury; y++) {
  	w->cury = y;
  	(void) _wclreol(w);
  }
  w->curx = ocurx;
  w->cury = ocury;
  wclrbol(w);
}

/*
 * Clear a window.
 */
void winclr(w)
WIN *w;
{
  register y;
  int olddir = w->direct;

  _setattr(w->attr, w->color);
  w->curx = 0;

  if (CL && w->y1 == 0 && w->y2 == LINES-1 &&
            w->x1 == 0 && w->x2 == COLS-1) {
	w->direct = 0;
	curx = 0;
	cury = 0;
	outstr(CL);
  }
  for(y = w->ys - 1; y >= 0; y--) {
 	w->cury = y;
 	(void) _wclreol(w);
  }
  w->direct = olddir;
  _gotoxy(w->x1, w->y1);
  if (dirflush) wflush();
}

/* ==== Insert / Delete functions ==== */

void winsline(w)
WIN *w;
{
  int osy1, osy2;
  
  osy1 = w->sy1;
  osy2 = w->sy2;
  
  w->sy1 = w->y1 + w->cury;
  w->sy2 = w->y2;
  wscroll(w, S_DOWN);
  
  w->sy1 = osy1;
  w->sy2 = osy2;
}

void wdelline(w)
WIN *w;
{
  int osy1, osy2;
  int ocury;
  
  ocury = w->cury;
  osy1 = w->sy1;
  osy2 = w->sy2;
  
  w->sy1 = w->y1 + w->cury;
  w->sy2 = w->y2;
  
  _intern = 1;
  wscroll(w, S_UP);
  _intern = 0;
  wlocate(w, 0, ocury);

  w->sy1 = osy1;
  w->sy2 = osy2;
}

/*
 * Insert a space at cursor position.
 */
void winschar(w)
WIN *w;
{
  int y;
  register x;
  int doit = 1;
  ELM buf[128];
  ELM *e = buf;

#ifdef SMOOTH
  curwin = w;
#endif

  if (w->curx == w->xs - 1) {
  	wputc(w, ' ');
  	return;
  }

  if (w->xs == COLS && IC != CNULL) {
  	doit = 0;
  	if (w->direct) outstr(IC);
  }
  
  /* Get the rest of line into buffer */
  y = w->y1 + w->cury;
  x = w->x1 + w->curx;
  memcpy(buf, gmap + COLS * y + x, sizeof(ELM) * (w->xs - w->curx));
  
  _write(' ', doit && w->direct, x++, y, w->attr, w->color);
  /* Write buffer to screen */
  for(; x <= w->x2; x++) {
  	_write(e->value, doit && w->direct, x, y, e->attr, e->color);
  	e++;
  }
  wlocate(w, w->curx, w->cury);
}

/*
 * Delete character under the cursor.
 */
void wdelchar(w)
WIN *w;
{
  register x, y;
  int doit = 1;
  ELM *e;

#ifdef SMOOTH
  curwin = w;
#endif

  x = w->x1 + w->curx;
  y = w->y1 + w->cury;
  
  if (w->direct && w->xs == COLS && DC != CNULL) {
  	/*_gotoxy(x - 1, y);*/
  	_gotoxy(x, y);
  	outstr(DC);
  	doit = 0;
  }
  
  e = gmap + y * COLS + x + 1;
  
  for(; x < w->x2; x++) {
  	_write(e->value, doit && w->direct, x, y, e->attr, e->color);
  	e++;
  }
  _write(' ', doit && w->direct, x, y, w->attr, w->color);
  wlocate(w, w->curx, w->cury);
}

/* ============= Support: edit a line on the screen. ============ */

/* Redraw the line we are editting. */
static void lredraw(w, x, y, s, len)
WIN *w;
int x;
int y;
char *s;
int len;
{
  int i, f;

  i = 0;
  wlocate(w, x, y);
  for(f = 0; f < len; f++) {
	if (s[f] == 0) i++;
	wputc(w, i ? ' ' : s[f]);
  }
}

/* wgets - edit one line in a window. */
int wgets(w, s, linelen, maxlen)
WIN *w;
char *s;
int linelen;
int maxlen;
{
  int c;
  int idx;
  int offs = 0;
  int f, st, i;
  char buf[256];
  int quit = 0;
  int x, y, r;
  int direct = dirflush;

  x = w->curx;
  y = w->cury;

  i = w->xs - x;
  if (linelen >= i - 1) linelen = i - 1;

  /* We assume the line has already been drawn on the screen. */
  if ((idx = strlen(s)) > linelen)
	idx = linelen;
  strcpy(buf, s);
  wlocate(w, x + idx, y);
  dirflush = 0;
  wflush();

  while(!quit) {
	c = getch();
	switch(c) {
		case '\r':
		case '\n':
			st = 0;
			quit = 1;
			break;
		case K_ESC: /* Exit without changing. */
			wlocate(w, x, y);
			lredraw(w, x, y, s, linelen);
			wflush();
			st = -1;
			quit = 1;
			break;
		case K_HOME: /* Home */
			r = (offs > 0);
			offs = 0;
			idx = 0;
			if (r) lredraw(w, x, y, buf, linelen);
			wlocate(w, x, y);
			wflush();
			break;
		case K_END: /* End of line. */
			idx = strlen(buf);
			r = 0;
			while(idx - offs > linelen) {
				r = 1;
				offs += 4;
			}
			if (r) lredraw(w, x, y, buf + offs, linelen);
			wlocate(w, x + idx - offs, y);
			wflush();
			break;
		case K_LT: /* Cursor left. */
		case K_BS: /* Backspace is first left, then DEL. */
			if (idx == 0) break;
			idx--;
			if (idx < offs) {
				offs -= 4;
				/*if (c == K_LT) FIXME? */
					lredraw(w, x, y, buf + offs, linelen);
			}
			if(c == K_LT) {
				wlocate(w, x + idx - offs, y);
				wflush();
				break;
			}
			/*FALLTHRU*/
		case K_DEL: /* Delete character under cursor. */
			if (buf[idx] == 0) break;
			for(f = idx; buf[f]; f++)
				buf[f] = buf[f+1];
			lredraw(w, x + idx - offs, y, buf + idx,
				linelen - (idx - offs));
			wlocate(w, x + idx - offs, y);
			wflush();
			break;
		case K_RT:
			if (buf[idx] == 0) break;
			idx++;
			if (idx - offs > linelen) {
				offs += 4;
				lredraw(w, x, y, buf + offs, linelen);
			}
			wlocate(w, x + idx - offs, y);
			wflush();
			break;
		default:
			/* Insert character at cursor position. */
			if (c < 32 || c > 127) break;
			if (idx + 1 >= maxlen) break;
			for(f = strlen(buf) + 1; f > idx; f--)
				buf[f] = buf[f-1];
			i = buf[idx];
			buf[idx] = c;
			if (i == 0) buf[idx+1] = i;
			if (idx - offs >= linelen) {
				offs += 4;
				lredraw(w, x, y, buf + offs, linelen);
			} else
				lredraw(w, x + idx - offs, y, buf + idx,
					linelen - (idx - offs));
			idx++;
			wlocate(w, x + idx - offs, y);
			wflush();
			break;
	}
  }
  if (st == 0) strcpy(s, buf);
  dirflush = direct;
  return(st);
}

/* ==== Initialization code ==== */

static char tbuf[1024];
static char cbuf[1024];

/*
 * Initialize the window system
 */
#ifdef BBS
/* Code for the BBS system.. */
int win_init(term, lines)
char *term;
int lines;
{
  int fg = WHITE;
  int bg = BLACK;
  int attr = A_NORMAL;
#else
/* Code for other applications (minicom!) */
int win_init(fg, bg, attr)
int fg;
int bg;
int attr;
{
  char *term;
#endif
  static WIN _stdwin;
  char *p;
  int f, olduseattr;

  if (w_init) return(0);

#ifndef BBS
  if ((term = getenv("TERM")) == CNULL) {
  	fprintf(stderr, "Environment variable TERM not set\n");
	return(-1);
  }
#endif
  switch((f = tgetent(cbuf, term))) {
  	case 0:
  		fprintf(stderr, "No termcap entry for %s\n", term);
  		return(-1);
  	case -1:
  		fprintf(stderr, "No /etc/termcap present!\n");
  		return(-1);
  	default:
  		break;
  }
  _tptr = tbuf;

  if ((CM = tgetstr("cm", &_tptr)) == CNULL) {
  	fprintf(stderr, "No cursor motion capability (cm)\n");
  	return(-1);
  }
  getrowcols(&LINES, &COLS);
#ifdef BBS
  LINES = lines;
#endif
  if (LINES == 0 && (LINES = tgetnum("li")) <= 0) {
  	fprintf(stderr, "Number of terminal lines unknown\n");
  	return(-1);
  }
  if (COLS == 0 && (COLS = tgetnum("co")) <= 0) {
  	fprintf(stderr, "Number of terminal columns unknown\n");
  	return(-1);
  }

  /* Terminal Capabilities */
  ME = tgetstr("me", &_tptr);
  SE = tgetstr("se", &_tptr);
  UE = tgetstr("ue", &_tptr);
  AS = tgetstr("as", &_tptr);
  AE = tgetstr("ae", &_tptr);
  MB = tgetstr("mb", &_tptr);
  MD = tgetstr("md", &_tptr);
  MR = tgetstr("mr", &_tptr);
  SO = tgetstr("so", &_tptr);
  US = tgetstr("us", &_tptr);
  CE = tgetstr("ce", &_tptr);
  Al = tgetstr("al", &_tptr);
  Dl = tgetstr("dl", &_tptr);
  AL = tgetstr("AL", &_tptr);
  DL = tgetstr("DL", &_tptr);
  CS = tgetstr("cs", &_tptr);
  SF = tgetstr("sf", &_tptr);
  SR = tgetstr("sr", &_tptr);
  VB = tgetstr("vb", &_tptr);
  VE = tgetstr("ve", &_tptr);
  VI = tgetstr("vi", &_tptr);
  IS = tgetstr("is", &_tptr);
  RS = tgetstr("rs", &_tptr);
  KS = tgetstr("ks", &_tptr);
  KE = tgetstr("ke", &_tptr);
  CD = tgetstr("cd", &_tptr);
  CL = tgetstr("cl", &_tptr);
  IC = tgetstr("ic", &_tptr);
  DC = tgetstr("dc", &_tptr);
  BC = tgetstr("bc", &_tptr);
  CR = tgetstr("cr", &_tptr);
  NL = tgetstr("nl", &_tptr);

  if (MR == CNULL) MR = SO;  /* Try standout */
  if (MR == CNULL) MR = US;  /* Try underline */
  if (MR == CNULL) MR = MD;  /* Try bold */
  if (SF == CNULL) SF = "\n";

  /* cr and nl are often not defined but result in great optimization.
   * I only hope that minicom does not break on terminals where this
   * really does not work..
   */
  if (CR == CNULL) CR = "\r";
  if (NL == CNULL) NL = "\n";

  /* Reset attributes */
  olduseattr = useattr;
  useattr = 1;
  _setattr(A_NORMAL, COLATTR(WHITE, BLACK));
  useattr = olduseattr;

  /* No reverse? don't use attributes at all. */
  if (MR == CNULL) useattr = 0;

  /* If we have the "ug" flag, don't allow attributes to be displayed. */
  if (tgetnum("ug") > 0) useattr = 0;
  
  _has_am = tgetflag("am");
  _mv_standout = tgetflag("ms");
  if (tgetflag("bs"))
	if (BC == CNULL) BC = "\b";
  else
  	BC = CNULL;	

  /* Special box-drawing characters */
  D_UL  = 201;
  D_HOR = 205;
  D_UR  = 187;
  D_LL  = 200;
  D_VER = 186;
  D_LR  = 188;
  
  S_UL  = 218;
  S_HOR = 196;
  S_UR  = 191;
  S_LL  = 192;
  S_VER = 179;
  S_LR  = 217;
  
  if ((p = tgetstr("gA", &_tptr)) != CNULL) wcharmap[201] = *p;
  if ((p = tgetstr("gB", &_tptr)) != CNULL) wcharmap[205] = *p;
  if ((p = tgetstr("gC", &_tptr)) != CNULL) wcharmap[187] = *p;
  if ((p = tgetstr("gD", &_tptr)) != CNULL) wcharmap[200] = *p;
  if ((p = tgetstr("gE", &_tptr)) != CNULL) wcharmap[186] = *p;
  if ((p = tgetstr("gF", &_tptr)) != CNULL) wcharmap[188] = *p;
  if ((p = tgetstr("gG", &_tptr)) != CNULL) wcharmap[218] = *p;
  if ((p = tgetstr("gH", &_tptr)) != CNULL) wcharmap[196] = *p;
  if ((p = tgetstr("gI", &_tptr)) != CNULL) wcharmap[191] = *p;
  if ((p = tgetstr("gJ", &_tptr)) != CNULL) wcharmap[192] = *p;
  if ((p = tgetstr("gK", &_tptr)) != CNULL) wcharmap[179] = *p;
  if ((p = tgetstr("gL", &_tptr)) != CNULL) wcharmap[217] = *p;

  /* Memory for global map */
  if ((gmap = (ELM *)malloc(sizeof(ELM) * LINES * COLS)) == (ELM *)0) {
  	fprintf(stderr, "Not enough memory\n");
  	return(-1);
  };
  _buffend = _bufstart + BUFFERSIZE;

  /* Initialize stdwin */
  stdwin = &_stdwin;

  stdwin->wrap = 1;
  stdwin->cursor = CNORMAL;
  stdwin->autocr = 1;
  stdwin->doscroll = 1;
  stdwin->x1 = 0;
  stdwin->sy1 = stdwin->y1 = 0;
  stdwin->x2 = COLS - 1;
  stdwin->sy2 = stdwin->y2 = LINES - 1;
  stdwin->xs = COLS;
  stdwin->ys = LINES;
  stdwin->attr = attr;
  stdwin->color = COLATTR(fg, bg);
  stdwin->direct = 1;
#if HISTORY
  stdwin->histbuf = (ELM *)0;
#endif

  if (KS != CNULL) outstr(KS); /* Keypad mode */
  if (IS != CNULL) outstr(IS); /* Initialization string */
  
  (void) setcbreak(1); /* Cbreak, no echo */

  winclr(stdwin);
  w_init = 1;
  return(0);
}

void win_end()
{
  if (gmap == (ELM *)0 || w_init == 0) return;
  (void) setcbreak(0); /* Reset */
  stdwin->attr = A_NORMAL;
  stdwin->color = COLATTR(WHITE, BLACK);
  _setattr(stdwin->attr, stdwin->color);
  winclr(stdwin);
  wcursor(stdwin, CNORMAL);
  if (KE != CNULL) outstr(KE);
  if (RS != CNULL) outstr(RS);
  wflush();
  free(gmap);
  gmap = (ELM *)0;
  stdwin = NIL_WIN;
  w_init = 0;
}
