/*
 * VT100.C	ANSI/VT102 emulator code.
 *		This code was integrated to the Minicom communications
 *		package, but has been reworked to allow usage as a separate
 *		module.
 *
 *		(C) 1993 Miquel van Smoorenburg.
 */
#include <sys/types.h>
#if defined (_POSIX_SOURCE) || defined(_BSD43)
#  include <stdlib.h>
#  include <unistd.h>
#  undef NULL
#endif
#include <time.h>
#include <stdio.h>
#include <signal.h>
#include <setjmp.h>
#include <string.h>
#include "window.h"
#include "vt100.h"

/*
 * The global variable esc_s holds the escape sequence status:
 * 0 - normal
 * 1 - ESC
 * 2 - ESC [
 * 3 - ESC [ ?
 * 4 - ESC (
 * 5 - ESC )
 * 6 - ESC #
 * 7 - ESC P
 */
static int esc_s = 0;

#define ESC 27

/* Structure to hold escape sequences. */
struct escseq {
  int code;
  char *vt100_st;
  char *vt100_app;
  char *ansi;
};

/* Escape sequences for different terminal types. */
static struct escseq vt_keys[] = {
  { K_F1,	"OP",	"OP",	"OP" },
  { K_F2,	"OQ",	"OQ",	"OQ" },
  { K_F3,	"OR",	"OR",	"OR" },
  { K_F4,	"OS",	"OS",	"OS" },
  { K_F5,	"",	"",	"OT" },
  { K_F6,	"",	"",	"OU" },
  { K_F7,	"",	"",	"OV" },
  { K_F8,	"",	"",	"OW" },
  { K_F9,	"",	"",	"OX" },
  { K_F10,	"",	"",	"OY" },
  { K_HOME,	"[H",	"[H",	"[H" },
  { K_PGUP,	"",	"",	"[V" },
  { K_UP,	"[A",	"OA",	"[A" },
  { K_LT,	"[D",	"OD",	"[D" },
  { K_RT,	"[C",	"OC",	"[C" },
  { K_DN,	"[B",	"OB",	"[B" },
  { K_END,	"[K",	"[K",	"[Y" },
  { K_PGDN,	"",	"",	"[U" },
  { K_INS,	"",	"",	"0"  },
  { K_DEL,	"\177",	"\177",	"\177" },
  { 0,		NULL,	NULL,	NULL },
};

#ifndef _SELECT
/* This is cheaper. */
#  define v_termout termout
#endif

#ifdef _SELECT
static int vt_echo;		/* Local echo on/off. */
#endif
static int vt_type   = ANSI;	/* Terminal type. */
static int vt_wrap   = 0;	/* Line wrap on/off */
static int vt_addlf  = 0;	/* Add linefeed on/off */
static int vt_fg;		/* Standard foreground color. */
static int vt_bg;		/* Standard background color. */
static int vt_keypad;		/* Keypad mode. */
static int vt_cursor;		/* cursor key mode. */
static int vt_bs = 8;		/* Code that backspace key sends. */
WIN *vt_win          = NIL_WIN;	/* Output window. */
static int vt_docap;		/* Capture on/off. */
static FILE *vt_capfp;		/* Capture file. */
static void (*vt_keyb)();	/* Gets called for NORMAL/APPL switch. */
static void (*termout)();	/* Gets called to output a string. */

static int escparms[8];		/* Cumulated escape sequence. */
static int ptr = -2;		/* Index into escparms array. */

short newy1 = 0;		/* Current size of scrolling region. */
short newy2 = 23;

/* Saved color and posistions */
static short savex = 0, savey = 0, saveattr = A_NORMAL, savecol = 112;

/*
 * Initialize the emulator once.
 */
void vt_install(fun1, fun2, win)
void (*fun1)();
void (*fun2)();
WIN *win;
{
  termout = fun1;
  vt_keyb = fun2;
  vt_win = win;
}

/* Set characteristics of emulator. */
void vt_init(type, fg, bg, wrap, add)
int type;
int fg;
int bg;
int wrap;
int add;
{
  vt_type = type;
  vt_fg = fg;
  vt_bg = bg;
  if (wrap >= 0) vt_win->wrap = vt_wrap = wrap;
  vt_addlf = add;

  newy1 = 0;
  newy2 = vt_win->ys - 1;
  wresetregion(vt_win);
  vt_keypad = NORMAL;
  vt_cursor = NORMAL;
  vt_echo = 0;
  ptr = -2;
  esc_s = 0;

  if (vt_keyb) vt_keyb(vt_keypad, vt_cursor);
  wsetfgcol(vt_win, vt_fg);
  wsetbgcol(vt_win, vt_bg);
}

/* Change some things on the fly. */
void vt_set(addlf, wrap, capfp, docap, bscode, echo, cursor)
int addlf;
int wrap;
FILE *capfp;
int docap;
int bscode;
int echo;
int cursor;
{
  if (addlf >= 0) vt_addlf = addlf;
  if (wrap >= 0)  vt_win->wrap = vt_wrap = wrap;
  if (capfp != (FILE *)0) vt_capfp = capfp;
  if (docap >= 0) vt_docap = docap;
  if (bscode >= 0) vt_bs = bscode;
  if (echo >= 0) vt_echo = echo;
  if (cursor >= 0) vt_cursor = cursor;
}

/* Output a string to the modem. */
#ifdef _SELECT
static void v_termout(s)
char *s;
{
  char *p;

  if (vt_echo) {
	for(p = s; *p; p++) vt_out(*p);
	wflush();
  }
  termout(s);
}
#endif

/*
 * Escape code handling.
 */

/*
 * ESC was seen the last time. Process the next character.
 */
static void state1(c)
int c;
{
  short x, y, f;

  switch(c) {
	case '[': /* ESC [ */
		esc_s = 2;
		return;
	case '(': /* ESC ( */
		esc_s = 4;
		return;
	case ')': /* ESC ) */
		esc_s = 5;
		return;
	case '#': /* ESC # */
		esc_s = 6;
		return;
	case 'P': /* ESC P (DCS, Device Control String) */
		esc_s = 7;
		return;
	case 'D': /* Cursor down */
	case 'M': /* Cursor up */
		x = vt_win->curx;
		if (c == 'D')  { /* Down. */
			y = vt_win->cury + 1;
			if (y == newy2 + 1)
				wscroll(vt_win, S_UP);
			else if (vt_win->cury < vt_win->ys)
				wlocate(vt_win, x, y);
		}		
		if (c == 'M')  { /* Up. */
			y = vt_win->cury - 1;
			if (y == newy1 - 1)
				wscroll(vt_win, S_DOWN);
			else if (y >= 0)
				wlocate(vt_win, x, y);
		}
		break;
	case 'E': /* CR + NL */
 		wputs(vt_win, "\r\n");
 		break;
 	case '7': /* Save attributes and cursor position */
 	case 's':
 		savex = vt_win->curx;
 		savey = vt_win->cury;
 		saveattr = vt_win->attr;
 		savecol = vt_win->color;
 		break;
 	case '8': /* Restore them */
 	case 'u':	
 		vt_win->color = savecol; /* HACK should use wsetfgcol etc */
 		wsetattr(vt_win, saveattr);
 		wlocate(vt_win, savex, savey);
 		break;
 	case '=': /* Keypad into applications mode */
		vt_keypad = APPL;
		if (vt_keyb) vt_keyb(vt_keypad, vt_cursor);
 		break;
 	case '>': /* Keypad into numeric mode */
 		vt_keypad = NORMAL;
		if (vt_keyb) vt_keyb(vt_keypad, vt_cursor);
 		break;
 	case 'Z': /* Report terminal type */
		if (vt_type == VT100)
			v_termout("\033[?1;0c");
 		else	
 			v_termout("\033[?c");
 		break;	
 	case 'c': /* Reset to initial state */
 		f = A_NORMAL;
 		wsetattr(vt_win, f);
 		wlocate(vt_win, 0, 0);
		vt_win->wrap = (vt_type != VT100);
		if (vt_wrap != -1) vt_win->wrap = vt_wrap;
		vt_init(vt_type, vt_fg, vt_bg, vt_win->wrap, 0);
 		break;
 	case 'N': /* G2 character set for next character only*/
 	case 'O': /* G3 "				"    */
 	case 'H': /* Set tab in current position */
 	case '<': /* Exit vt52 mode */
 	default:
 		/* ALL IGNORED */
 		break;
  }
  esc_s = 0;
  return;
}

/*
 * ESC [ ... was seen the last time. Process next character.
 */
static void state2(c)
int c;
{
  short x, y, attr, f;
  char temp[16];
  char did_esc = 1;

  /* See if a number follows */
  if (c >= '0' && c <= '9') {
	if (ptr < 0) ptr = 0;
	escparms[ptr] = 10*(escparms[ptr]) + c - '0';
	return;
  }
  /* Separation between numbers ? */
  if (c == ';') {
	if (ptr < 15 && ptr >= 0) ptr++;
	return;
  }
  /* ESC [ something-without-argument? */
  if (ptr < 0) switch(c) {
	case '?': /* ESC [ ? */
		esc_s = 3;
		return;
	case 'K': /* Clear to end of line */
		wclreol(vt_win);
		break;
	case 'J': /* Clear to end of screen */
		wclreos(vt_win);
		break;
	case 'c': /* Identify Terminal Type */
		if (vt_type == VT100)
			v_termout("\033[?1;0c");
		else	
			v_termout("\033[?c");
		break;	
	case 'i': /* print page */
	case 'g': /* clear tab stop */
		/* IGNORED */
		break;
 	case 's': /* Save attributes and cursor position */
 		savex = vt_win->curx;
 		savey = vt_win->cury;
 		saveattr = vt_win->attr;
 		savecol = vt_win->color;
 		break;
 	case 'u': /* Restore them */
 		vt_win->color = savecol; /* HACK should use wsetfgcol etc */
 		wsetattr(vt_win, saveattr);
 		wlocate(vt_win, savex, savey);
 		break;
	default: /* We did not find it. Maybe it's a variable-argument func. */
		did_esc = 0;
		break;
  }
  if (ptr < 0 && did_esc) {
	esc_s = 0;
	return;
  }
  /* ESC [ one-argument-only something ? */
  if (ptr == 0) switch(c) {
	case 'K': /* Line erasing */
		switch(escparms[0]) {
			case 0:
				wclreol(vt_win);
				break;
			case 1:
				wclrbol(vt_win);
				break;
			case 2:
				wclrel(vt_win);
				break;
		}
		break;
	case 'J': /* Screen erasing */
		x = vt_win->color;
		y = vt_win->attr;
		if (vt_type == ANSI) {
			wsetattr(vt_win, A_NORMAL);
			wsetfgcol(vt_win, WHITE);
			wsetbgcol(vt_win, BLACK);
		}
		switch(escparms[0]) {
			case 0:
				wclreos(vt_win);
				break;
			case 1:
				wclrbos(vt_win);
				break;
			case 2:
				winclr(vt_win);
				break;
		}
		if (vt_type == ANSI) {
			vt_win->color = x;
			vt_win->attr = y;
		}
		break;
	case 'n': /* Requests / Reports */
		switch(escparms[0]) {
			case 5: /* Status */
				v_termout("\033[0n");
				break;
			case 6:	/* Cursor Position */
				sprintf(temp, "\033[%d;%dR", 
					vt_win->cury + 1, vt_win->curx + 1);
				v_termout(temp);
				break;
		}
		break;
	case 'c': /* Identify Terminal Type */
		if (vt_type == VT100) {
			v_termout("\033[?1;0c");
			break;
		}
		v_termout("\033[?c");
		break;
	case 'g': /* Tabulation */
	case 'i': /* Printing */
	default:
		/* IGNORED */
		break;
  }

  /* Process functions with zero, one, two or more arguments */
  switch(c) {
	case 'A':
	case 'B':
	case 'C':
	case 'D': /* Cursor motion */
		if ((f = escparms[0]) == 0) f = 1;
		x = vt_win->curx;
		y = vt_win->cury;
		x += f * ((c == 'C') - (c == 'D'));
		if (x < 0) x = 0;
		if (x >= vt_win->xs) x = vt_win->xs - 1;
		if (c == 'B') { /* Down. */
			y += f;
			if (y >= vt_win->ys) y = vt_win->ys - 1;
			if (y == newy2 + 1) y = newy2;
		}
		if (c == 'A') { /* Up. */
			y -= f;
	 		if (y < 0) y = 0;
			if (y == newy1 - 1) y = newy1;
		}	
		wlocate(vt_win, x, y);
		break;
	case 'H':
	case 'f': /* Set cursor position */
		if ((y = escparms[0]) == 0) y = 1;
		if ((x = escparms[1]) == 0) x = 1;
		wlocate(vt_win, x - 1, y - 1);
		break;
	case 'm': /* Set attributes */
		  /* Without argument, esc-parms[0] is 0 */
		if (ptr < 0) ptr = 0;  
		attr = wgetattr((vt_win));
		for (f = 0; f <= ptr; f++) {
		    if (escparms[f] >= 30 && escparms[f] <= 37)
			wsetfgcol(vt_win, escparms[f] - 30);
		    if (escparms[f] >= 40 && escparms[f] <= 47)
			wsetbgcol(vt_win, escparms[f] - 40);
		    switch(escparms[f]) {
			case 0:
				attr = A_NORMAL;
				wsetfgcol(vt_win, vt_fg);
				wsetbgcol(vt_win, vt_bg);
				break;
			case 4:
				attr |= A_UNDERLINE;
				break;
			case 7:
				attr |= A_REVERSE;
				break;
			case 1:
				attr |= A_BOLD;
				break;
			case 5:
				attr |= A_BLINK;
				break;
			case 22: /* Bold off */
				attr &= ~A_BOLD;
				break;
			case 24: /* Not underlined */
				attr &=~A_UNDERLINE;
				break;
			case 25: /* Not blinking */
				attr &= ~A_BLINK;
				break;
			case 27: /* Not reverse */
				attr &= ~A_REVERSE;
				break;
			case 39: /* Default fg color */
				wsetfgcol(vt_win, vt_fg);
				break;
			case 49: /* Default bg color */
				wsetbgcol(vt_win, vt_bg);
				break;
				
		    }
		}
		wsetattr(vt_win, attr);
		break;
	case 'L': /* Insert lines */
		if ((x = escparms[0]) == 0) x = 1;
		for(f = 0; f < x; f++)
			winsline(vt_win);
		break;	
	case 'M': /* Delete lines */
		if ((x = escparms[0]) == 0) x = 1;
		for(f = 0; f < x; f++)
			wdelline(vt_win);
		break;	
	case 'P': /* Delete Characters */
		if ((x = escparms[0]) == 0) x = 1;
		for(f = 0; f < x; f++)
			wdelchar(vt_win);
		break;	
	case '@': /* Insert Characters */		
		if ((x = escparms[0]) == 0) x = 1;
		for(f = 0; f < x; f++)
			winschar(vt_win);
		break;	
	case 'r': /* Set scroll region */
		if ((newy1 = escparms[0]) == 0) newy1 = 1;
		if ((newy2 = escparms[1]) == 0) newy2 = vt_win->ys;
		newy1-- ; newy2--;
		if (newy1 < 0) newy1 = 0;
		if (newy2 < 0) newy2 = 0;
		if (newy1 >= vt_win->ys) newy1 = vt_win->ys - 1;
		if (newy2 >= vt_win->ys) newy2 = vt_win->ys - 1;
		wsetregion(vt_win, newy1, newy2);
		break;
	case 'y': /* Self test modes */
	default:
		/* IGNORED */
		break;
  }
  /* Ok, our escape sequence is all done */
  esc_s = 0;
  ptr = -2;
  return;		
}
  
/*
 * ESC [ ? ... seen.
 */
static void state3(c)
int c;
{
  /* See if a number follows */
  if (c >= '0' && c <= '9') {
	if (ptr < 0) ptr = 0;
	escparms[ptr] = 10*(escparms[ptr]) + c - '0';
	return;
  }
  /* ESC [ ? number seen */
  if (ptr < 0) {
	esc_s = 0;
	return;
  }
  switch(c) {
	case 'h':
		switch(escparms[0]) {
			case 7: /* Auto wrap on (automatic margins) */
				vt_win->wrap = 1;
				break;
			case 6: /* Set scroll region */
				if (newy1 < 0) newy1 = 0;
				if (newy2 < 0) newy2 = 0;
				if (newy1 >= vt_win->ys) newy1 = vt_win->ys - 1;
				if (newy2 >= vt_win->ys) newy2 = vt_win->ys - 1;
				wsetregion(vt_win, newy1, newy2);
				break;
			case 1: /* Cursor keys in appl. mode */
				vt_cursor = APPL;
				if (vt_keyb) vt_keyb(vt_keypad, vt_cursor);
				break;
			case 25: /* Cursor on */
				wcursor(vt_win, CNORMAL);
				break;	
			default: /* Mostly set up functions */
				/* IGNORED */
				break;
		}
		break;
	case 'l':
		switch(escparms[0]) {
			case 7: /* Auto wrap off */
				vt_win->wrap = 0;
				break;
			case 6: /* Whole screen mode */
				newy1 = 0;
				newy2 = vt_win->ys - 1;
				wresetregion(vt_win);
				break;
			case 1: /* Cursor keys in cursor pos. mode */
				vt_cursor = NORMAL;
				if (vt_keyb) vt_keyb(vt_keypad, vt_cursor);
				break;
			case 25: /* Cursor off */
				wcursor(vt_win, CNONE);
				break;
			default: /* Mostly set up functions */
				/* IGNORED */
				break;
		}
		break;
	case 'i': /* Printing */
	case 'n': /* Request printer status */
	default:
		/* IGNORED */
		break;
  }
  esc_s = 0;
  ptr = -2;
  return;
}

/*
 * ESC ( Seen.
 */
static void state4(c)
int c;
{
  /* Switch Character Sets. */
  /* IGNORED */
  esc_s = 0;
}

/*
 * ESC ) Seen.
 */
static void state5(c)
int c;
{
  /* Switch Character Sets. */
  /* IGNORED */
  esc_s = 0;
}

/*
 * ESC # Seen.
 */
static void state6(c)
int c;
{
  /* Double Height and Double width character sets */
  /* IGNORED */
  esc_s = 0;
}

/*
 * ESC P Seen.
 */
static void state7(c)
int c;
{
  /*
   * Device dependant control strings. The Minix virtual console package
   * uses these sequences. We can only turn cursor on or off, because
   * that's the only one supported in termcap. The rest is ignored.
   */
  static char buf[17];
  static int pos = 0;
  static int state = 0;

  if (c == ESC) {
  	state = 1;
  	return;
  }
  if (state == 1) {
  	buf[pos] = 0;
  	pos = 0;
  	state = 0;
  	esc_s = 0;
  	if (c != '\\') return;
  	/* Process string here! */
  	if (!strcmp(buf, "cursor.on")) wcursor(vt_win, CNORMAL);
  	if (!strcmp(buf, "cursor.off")) wcursor(vt_win, CNONE);
  	if (!strcmp(buf, "linewrap.on")) {
  		vt_wrap = -1;
  		vt_win->wrap = 1;
  	}
  	if (!strcmp(buf, "linewrap.off")) {
  		vt_wrap = -1;
  		vt_win->wrap = 0;
  	}
  	return;
  }
  if (pos > 15) return;
  buf[pos++] = c;
}

void vt_out(ch)
int ch;
{
  int f;
  short x;
  unsigned char c;

  if (!ch) return;

  if (ptr == -2) { /* Initialize */
	ptr = -1;
	for(f = 0; f < 8; f++) escparms[f] = 0;
  }

  c = (unsigned char)ch;
  
  if (vt_docap == 2) /* Literal. */
	fputc(c, vt_capfp);

  switch(esc_s) {
	case 0: /* Normal character */
		switch(c) {
			case '\r': /* Carriage return */
				wputc(vt_win, c);
				if (vt_addlf) {
					wputc(vt_win, '\n');
					if (vt_docap == 1)
						fputc('\n', vt_capfp);
				}
				break;
			case '\t': /* Non - destructive TAB */
				x = ((vt_win->curx / 8) + 1) * 8;
				wlocate(vt_win, x, vt_win->cury);
				if (vt_docap == 1) fputc(c, vt_capfp);
				break;
			case 013: /* Old Minix: CTRL-K = up */
				wlocate(vt_win, vt_win->curx, vt_win->cury - 1);
				break;
			case '\f': /* Form feed: clear screen. */
				winclr(vt_win);
				wlocate(vt_win, 0, 0);
				break;
			case 14:
			case 15:  /* Change character set. Not supported. */
				break;
			case ESC: /* Begin escape sequence */
				esc_s = 1;
				break;
			case '\n': /* Printable by wputc too */
			case '\b':
			case 7: /* Bell */
			default: /* Printable character */
				wputc(vt_win, c);
				if (vt_docap == 1)
					fputc(c, vt_capfp);
				break;
		}
		break;
	case 1: /* ESC seen */
		state1(c);
		break;
	case 2: /* ESC [ ... seen */
		state2(c);
		break;
	case 3:
		state3(c);
		break;
	case 4:
		state4(c);
		break;
	case 5:
		state5(c);
		break;
	case 6:
		state6(c);
		break;
	case 7:
		state7(c);
		break;	
  }
}

/* Translate keycode to escape sequence. */
void vt_send(c)
int c;
{
  char s[2];
  int f;

  /* Special key? */
  if (c < 256) {
	/* Translate backapce key? */
	if (c == K_ERA) c = vt_bs;
	s[0] = c;
	s[1] = 0;
	v_termout(s);
	return;
  }

  /* Look up code in translation table. */
  for(f = 0; vt_keys[f].code; f++)
	if (vt_keys[f].code == c) break;
  if (vt_keys[f].code == 0) return;

  /* Now send appropriate escape code. */
  v_termout("\033");
  if (vt_type == VT100) {
	if (vt_cursor == NORMAL)
		v_termout(vt_keys[f].vt100_st);
	else
		v_termout(vt_keys[f].vt100_app);
  } else
	v_termout(vt_keys[f].ansi);
}
