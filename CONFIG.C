/*
 * This file is part of the Minicom Communications Program,
 * written by Miquel van Smoorenburg 1991/1992/1993.
 */
#include <sys/types.h>
#if defined _POSIX_SOURCE || defined(_BSD43)
#  include <unistd.h>
#  include <stdlib.h>
#endif
#ifdef _MINIX
#  undef NULL			/* Kludge */
#endif
#include <stdio.h>
#include <setjmp.h>
#ifdef _MINIX
#  undef NULL			/* Kludge */
#endif
#include <string.h>
#if defined (_MINIX) && !defined(NULL)
#  define NULL ((void *)0)	/* Kludge */
#endif
#include <sys/stat.h>
#if defined(_BSD43) || defined (_SYSV)
#  define NOSTREAMS
#  include <sys/file.h>
#endif
#include <ctype.h>
#include "window.h"
#include "minicom.h"
#include "configsym.h"
#include "keyboard.h"
#include "vt100.h"

/*
 * Configure minicom.
 */

/*
 * Read in parameters.
 */
void read_parms()
{
  FILE *fp;
  int f;
  char buf[64];
  char *p;

  /* Read global parameters */
  if ((fp = fopen(parfile, "r")) == (FILE *)NULL) {
  	if (real_uid == 0) {
  		fprintf(stderr,
  	"minicom: WARNING: configuration file not found, using defaults\n");
  		sleep(1);
#ifdef _ACK
		/* Initialize structure by hand */
		readpars((FILE *)NULL, 1);
#endif
  		return;
  	}
  	fprintf(stderr,
	"minicom: there is no global configuration file %s\n", parfile);
  	fprintf(stderr, "Ask your sysadm to create one (with minicom -s).\n");
  	exit(1);
  }
  readpars(fp, 1);
  fclose(fp);
  /* Read personal parameters */
  if ((fp = fopen(pparfile, "r")) != (FILE *)NULL) {
	readpars(fp, 0);
	fclose(fp);
  }

  /* This code is to use old configuration files. */
  for(f = PROTO_BASE; f < MAXPROTO; f++) {
	if (P_PNAME(f)[0] && P_PIORED(f) != 'Y' && P_PIORED(f) != 'N') {
		strcpy(buf, P_PNAME(f) - 2);
		strcpy(P_PNAME(f), buf);
		P_PIORED(f) = 'Y';
		P_PFULL(f) = 'N';
	}
  }
  p = basename(P_LOCK);
  if (strncmp(p, "LCK", 3) == 0) *p = 0;
}

/*
 * See if we have write access to a file.
 * If it is not there, see if the directory is writable.
 */
int waccess(s)
char *s;
{
  char *p;
  char buf[128];
  struct stat stt;

  /* We use stat instead of access(s, F_OK) because I couldn't get
   * that to work under BSD 4.3 ...
   */
  if (stat(s, &stt) == 0) {
	if (access(s, W_OK) == 0)
		return(A_OK_EXIST);
	return(-1);
  }
  strcpy(buf, s);
  if((p = strrchr(buf, '/')) == (char *)NULL)
  	strcpy(buf, ".");
  else
  	*p = '\0';
  if (access(buf, W_OK) == 0)
	return(A_OK_NOTEXIST);
  return(-1);
}

/*
 * Read in a string, but first check to see if it's
 * allowed to do so.
 */
static void pgets(w, x, y, s, len, maxl)
WIN *w;
int x, y;
char *s;
int len;
int maxl;
{
  struct pars *p = (struct pars *)s;

  if ((p->flags & PRIVATE) && real_uid != 0) {
  	werror("You are not allowed to change this parameter");
  	return;
  }
  wlocate(w, x, y);
  (void) wgets(w, s, len, maxl);
  p->flags |= CHANGED;
}

/*
 * Mark a variable as changed.
 */
static void markch(s)
char *s;
{
  struct pars *p = (struct pars *)s;

  p->flags |= CHANGED;
}

/*
 * Set a string to a given value, but only if we're allowed to.
 */
static void psets(s, w)
char *s, *w;
{
  struct pars *p = (struct pars *)s;

  if ((p->flags & PRIVATE) && real_uid != 0) {
  	werror("You are not allowed to change this parameter");
  	return;
  }
  strcpy(s, w);
  p->flags |= CHANGED;
}

/*
 * Get a a character from the keyboard. Translate lower
 * to uppercase and '\r' to '\n'.
 */
static int rgetch()
{
  int c;

  c = getch();
  if (islower(c)) c = toupper(c);
  if (c == '\n' || c == '\r' || c == '\033') return('\n');
  return(c);
}

static void dopath()
{
  WIN *w;
  int c;
  
  w = wopen(5, 5, 75, 11, BDOUBLE, stdattr, MFG, MBG, 0, 0, 1);
  wprintf(w, " A - Download directory : %.44s\n", P_DOWNDIR);
  wprintf(w, " B -   Upload directory : %.44s\n", P_UPDIR);
  wprintf(w, " C -   Script directory : %.44s\n", P_SCRIPTDIR);
  wprintf(w, " D -     Script program : %.44s\n", P_SCRIPTPROG);
  wprintf(w, " E -     Kermit program : %.44s\n", P_KERMIT);
  wlocate(w, 4, 7);
  wputs(w, "Change which setting? ");

  wredraw(w, 1);

  while(1) {
      wlocate(w, 26, 7);
      c = rgetch();
      switch(c) {
  	case '\n':
  		wclose(w, 1);
  		return;
  	case 'A':
  		pgets(w, 26, 0, P_DOWNDIR, 64, 64);
  		break;
  	case 'B':
  		pgets(w, 26, 1, P_UPDIR, 64, 64);
  		break;
  	case 'C':
  		pgets(w, 26, 2, P_SCRIPTDIR, 64, 64);
  		break;
  	case 'D':
  		pgets(w, 26, 3, P_SCRIPTPROG, 64, 64);
  		break;
  	case 'E':
  		pgets(w, 30, 0, P_KERMIT, 64, 64);
  		break;
  	default:
  		break;
      }
  }
}

char *yesno(k)
int k;
{
  return(k ? "Yes" : "No ");
}

/*
 * Input the definition of an up/download protocol.
 */
static void inputproto(w, n)
WIN *w;
int n;
{
  int c = 0;

  mpars[PROTO_BASE + n].flags |= CHANGED;

  if (P_PNAME(n)[0] == '\0') {
  	P_PNN(n) = 'Y';
  	P_PUD(n) = 'U';
	P_PFULL(n) = 'N';
	P_PPROG(n)[0] = 0;
	P_PIORED(n) = 'Y';
  	wlocate(w, 4, n+1);
  	wputs(w, "       ");
  }
  wlocate(w, 4, n+1);
  (void ) wgets(w, P_PNAME(n), 10, 64);
  pgets(w, 15, n+1, P_PPROG(n), 31, 64);
  do {
	wlocate(w, 48, n+1);
	wprintf(w, "%c", P_PNN(n));
	c = rgetch();
	if (c == 'Y') P_PNN(n) = 'Y';
	if (c == 'N') P_PNN(n) = 'N';
  } while(c != '\r' && c != '\n');
  do {
	wlocate(w, 56, n+1);
	wprintf(w, "%c", P_PUD(n));
	c = rgetch();
	if (c == 'U') P_PUD(n) = 'U';
	if (c == 'D') P_PUD(n) = 'D';
  } while(c != '\r' && c != '\n');
  do {
	wlocate(w, 64, n+1);
	wprintf(w, "%c", P_PFULL(n));
	c = rgetch();
	if (c == 'Y') P_PFULL(n) = 'Y';
	if (c == 'N') P_PFULL(n) = 'N';
  } while(c != '\r' && c != '\n');
  do {
	wlocate(w, 72, n+1);
	wprintf(w, "%c", P_PIORED(n));
	c = rgetch();
	if (c == 'Y') P_PIORED(n) = 'Y';
	if (c == 'N') P_PIORED(n) = 'N';
  } while(c != '\r' && c != '\n');
}

static void doproto()
{
  WIN *w;
  int f, c;

  w = wopen(1, 4, 78, 18, BDOUBLE, stdattr, MFG, MBG, 0, 0, 1);
  wputs(w, "     Name             Program");
  wlocate(w, 44, 0);
  wputs(w, "Need name Up/Down FullScr IO-Red.");
  for(f = 0; f < 12; f++) {
     wlocate(w, 1, f+1);
     if (P_PNAME(f)[0])
  	wprintf(w, "%c  %-10.10s %-31.31s  %c       %c       %c       %c",
		'A' + f,
  		P_PNAME(f), P_PPROG(f),
  		P_PNN(f), P_PUD(f),
		P_PFULL(f), P_PIORED(f));
     else
        wprintf(w, "%c    -", 'A' + f);
  }
  wlocate(w, 3, 14);
  wputs(w, "Change which setting? (SPACE to delete) ");
  wredraw(w, 1);

  do {
  	wlocate(w, 43, 14);
  	c = rgetch();
  	if (c >= 'A' && c <= 'L') inputproto(w, c - 'A');
  	if (c == ' ') {
  		wlocate(w, 3, 14);
  		wputs(w, "Delete which protocol? ");
  		wclreol(w);
  		c = rgetch();
  		if (c >= 'A' && c <= 'L') {
  			P_PNAME(c - 'A')[0] = '\0';
  			mpars[PROTO_BASE + (c - 'A')].flags |= CHANGED;
  			wlocate(w, 3, c - 'A' + 1);
  			wclreol(w);
  			wputs(w, "   -");
  		}
		wlocate(w, 3, 14);
		wputs(w, "Change which setting? (SPACE to delete) ");
		c = ' ';
	}
  } while(c != '\n');
  wclose(w, 1);
}

static void doserial()
{
  WIN *w;

  w = wopen(5, 5, 75, 11, BDOUBLE, stdattr, MFG, MBG, 0, 0, 1);
  wprintf(w, " A -    Serial Device : %.46s\n", P_PORT);
  wprintf(w, " B - Lockfile Location: %.46s\n", P_LOCK);
  wprintf(w, " C -   Callin Program : %.46s\n", P_CALLIN);
  wprintf(w, " D -  Callout Program : %.46s\n", P_CALLOUT);
  wprintf(w, " E -    Baud/Par/Bits : %s %s%s1\n", P_BAUDRATE,
  	P_BITS, P_PARITY);
  wlocate(w, 4, 6);
  wputs(w, "Change which setting? ");
  wredraw(w, 1);

  while(1) {
      wlocate(w, 26, 6);
      switch(rgetch()) {
  	case '\n':
  		wclose(w, 1);
  		return;
  	case 'A':
  		pgets(w, 24, 0, P_PORT, 64, 64);
  		break;
  	case 'B':
  		pgets(w, 24, 1, P_LOCK, 64, 64);
  		break;
  	case 'C':
  		pgets(w, 24, 2, P_CALLIN, 64, 64);
  		break;
  	case 'D':
  		pgets(w, 24, 3, P_CALLOUT, 64, 64);
  		break;
  	case 'E':
  		get_bbp(P_BAUDRATE, P_BITS, P_PARITY);
  		if (portfd >= 0)
			m_setparms(portfd, P_BAUDRATE, P_PARITY, P_BITS);
  		wlocate(w, 24, 4);
		wprintf(w, "%s %s%s1  \n", P_BAUDRATE, P_BITS, P_PARITY);
		if (st != NIL_WIN) mode_status();
		markch(P_BAUDRATE);
		markch(P_BITS);
		markch(P_PARITY);
		break;
  	default:
  		break;
      }	
  }
}

static void domodem()
{
  WIN *w;
  char *str;
  int c, x, y, ypos, maxl;

  w = wopen(2, 2, 77, 21, BDOUBLE, stdattr, MFG, MBG, 0, 0, 1);

  dirflush = 0;
  wtitle(w, TMID, "Modem and dialing parameter setup");
  wprintf(w, "\n");
  wprintf(w, " A - Init string ......... %.48s\n", P_MINIT);
  wprintf(w, " B - Reset string ........ %.48s\n", P_MRESET);
  wprintf(w, " C - Dialing prefix #1.... %.48s\n", P_MDIALPRE);
  wprintf(w, " D - Dialing suffix #1.... %.48s\n", P_MDIALSUF);
  wprintf(w, " E - Dialing prefix #2.... %.48s\n", P_MDIALPRE2);
  wprintf(w, " F - Dialing suffix #2.... %.48s\n", P_MDIALSUF2);
  wprintf(w, " G - Dialing prefix #3.... %.48s\n", P_MDIALPRE3);
  wprintf(w, " H - Dialing suffix #3.... %.48s\n", P_MDIALSUF3);
  wprintf(w, " I - Connect string ...... %.48s\n", P_MCONNECT);
  wprintf(w, " J - No connect strings .. %-20.20s  %.20s\n", P_MNOCON1, P_MNOCON2);
  wlocate(w, 27, 11);
  wprintf(w, "%-20.20s  %.20s\n", P_MNOCON3, P_MNOCON4);
  wprintf(w, " K - Hang-up string ...... %.48s\n", P_MHANGUP);
  wprintf(w, " L - Dial cancel string .. %.48s\n", P_MDIALCAN);
  wprintf(w, "\n");
  wprintf(w, " M - Dial time ........... %.3s\n", P_MDIALTIME);
  wprintf(w, " N - Delay before redial . %.3s\n", P_MRDELAY);
  wprintf(w, " O - Number of tries ..... %.3s\n", P_MRETRIES);
  wlocate(w, 34, 15);
  wprintf(w, " P - Auto baud detect .... %s\n", P_MAUTOBAUD);
  wlocate(w, 34, 16);
  wprintf(w, " Q - Drop DTR to hangup .. %s\n", P_MDROPDTR);
  wlocate(w, 34, 17);
  wprintf(w, " R - Modem has DCD line .. %s\n", P_HASDCD);

  wlocate(w, 1, 19);
  wprintf(w, " Change which setting? ");
  x = w->curx; y = w->cury;
  wprintf(w, "      (Return or Esc to exit)");
  wredraw(w, 1);

  while(1) {
  	wlocate(w, x, y);
  	wflush();
  	c = rgetch();
  	ypos = 1;
	maxl = 64;
  	switch(c) {
  		case 'M':
  		case 'N':
  		case 'O':
  			ypos++;
			maxl = 4;
  		case 'K':
  		case 'L':
  			ypos -= 2;
  			c += 3;
  		case 'A':
  		case 'B':
  		case 'C':
  		case 'D':
  		case 'E':
		case 'F':
		case 'G':
		case 'H':
		case 'I':
  			/* Calculate adress of string tomodify */
  			str = P_MINIT + (c - 'A') * sizeof(struct pars);
  			pgets(w, 27, ypos + (c - 'A'), str, maxl, maxl);
  			break;
  		case 'J':
  			/* Walk through all four */
  			pgets(w, 27, 10, P_MNOCON1, 20, 64);
  			pgets(w, 49, 10, P_MNOCON2, 20, 64);
  			pgets(w, 27, 11, P_MNOCON3, 20, 64);
  			pgets(w, 49, 11, P_MNOCON4, 20, 64);
  			break;
  		case 'P':
  			psets(P_MAUTOBAUD, yesno(P_MAUTOBAUD[0] == 'N'));
  			wlocate(w, 61, 15);
  			wputs(w, P_MAUTOBAUD);
  			break;
  		case 'Q':
  			psets(P_MDROPDTR, yesno(P_MDROPDTR[0] == 'N'));
  			wlocate(w, 61, 16);
  			wputs(w, P_MDROPDTR);
  			break;
  		case 'R':
  			psets(P_HASDCD, yesno(P_HASDCD[0] == 'N'));
  			wlocate(w, 61, 17);
  			wputs(w, P_HASDCD);
  			break;
  		case '\n':
  			dirflush = 1;
  			wclose(w, 1);
  			return;
  		default:
  			break;	
  	}
  }
}


/*
 * Screen and keyboard menu.
 */
static void doscrkeyb()
{
  WIN *w;
  int c;
  int clr = 1;
  char buf[16];

  w = wopen(15, 8, 65, 14, BDOUBLE, stdattr, MFG, MBG, 0, 0, 1);
  
  wtitle(w, TMID, "Screen and keyboard");
  wprintf(w, "\n A - Command key is      : %s\n", P_ESCAPE);
  wprintf(w, " B - Backspace key sends : %s\n", P_BACKSPACE);
  wprintf(w, " C - Status line is      : %s", P_STATLINE);
  wredraw(w, 1);

  while(1) {
  	if (clr) {
  		wlocate(w, 2, 5);
		wputs(w, "Change which setting?  (Esc to exit) ");
		wclreol(w);
		clr = 0;
	} else
  		wlocate(w, 39, 5);
  	switch(rgetch()) {
  		case '\n':
  			wclose(w, 1);
  			return;
  		case 'A':
  			wlocate(w, 2, 5);
#if defined (_MINIX) || defined (_COHERENT) || defined (linux)
  			wputs(w, "Press new escape key, SPACE for ALT: ");
  			if ((c = getch()) == ' ')
  				strcpy(buf, "ALT");
  			else
  				sprintf(buf, "^%c", (c & 0x1f) + 'A' - 1);
#else
  			wputs(w, "Press new escape key, SPACE for MetaKey: ");
  			if ((c = getch()) == ' ')
  				strcpy(buf, "meta key");
  			else
  				sprintf(buf, "^%c", (c & 0x1f) + 'A' - 1);
#endif
  			psets(P_ESCAPE, buf);
  			wlocate(w, 27, 1);
  			wputs(w, P_ESCAPE);
  			if (c != ' ') wputs(w, "        ");
  			clr = 1;
  			if (st) {
  				/*
  				 * If minicom was called with the '-s' option,
  				 * keyserv does not run yet. We can see this
  				 * because the status line is not initialized
  				 * at that time, either.
  				 */
				keyboard(KSETESC, P_ESCAPE[0] == '^' ?
					P_ESCAPE[1] & 31 : 128);
  				show_status();
  			}
  			alt_override = 0;
  			break;
  		case 'B':
  			if (P_BACKSPACE[0] == 'D')
  				psets(P_BACKSPACE, "BS");
  			else
  				psets(P_BACKSPACE, "DEL");
  			wlocate(w, 27, 2);
  			wprintf(w, "%s ", P_BACKSPACE);
  			if (st)
				keyboard(KSETBS, P_BACKSPACE[0] == 'B' ?
								8 : 128);
  			break;
  		case 'C':
  			if (P_STATLINE[0] == 'e') {
  				psets(P_STATLINE, "disabled");
  				tempst = 1;
  			} else {
  				psets(P_STATLINE, "enabled");
  				/* See if it fits on screen */
  				if (LINES > 24 + (terminal == MINIX))
  					tempst = 0;
  			}
  			wlocate(w, 27, 3);
  			wprintf(w, "%s ", P_STATLINE);
  			break;
  	}
  }
}

/*
 * Save the configuration.
 */
static void dodflsave()
{
  FILE *fp;
  int am;

  /* Root saves new configuration */
  if (real_uid == 0) {
  	if ((fp = fopen(parfile, "w")) == (FILE *)NULL) {
  		werror("Cannot write to %s", parfile);
  		return;
  	}
  	writepars(fp, 1);
  } else {
	/* Mortals save their own configuration */
	if ((am = waccess(pparfile)) < 0 ||
		(fp = fopen(pparfile, "w")) == (FILE *)NULL) {
  		werror("Cannot write to %s", pparfile);
  		return;
	}
#ifndef DEBUG
#ifndef HAS_FCHOWN
	if (am == A_OK_NOTEXIST) chown(pparfile, real_uid, real_gid);
#else
	if (am == A_OK_NOTEXIST) fchown(fileno(fp), real_uid, real_gid);
#endif
	writepars(fp, 0);
#endif
  }
  fclose(fp);
  werror("Configuration saved");
}

/*
 * Save the configuration, ask a name for it.
 */
static void donamsave()
{
  char ifile[128];
  char *s;

  if (real_uid != 0) {
  	werror("You are not allowed to create a configuration");
	return;
  }

  ifile[0] = 0;
  s = input("Give name to save this configuration?", ifile);
  if (s != (char *)0 && *s != 0) {
  	sprintf(parfile, "%s/minirc.%s", LIBDIR, s);
  dodflsave();
  }
}

static void (*funcs1[])() = {
  dopath,
  doproto,
  doserial,
  domodem,
  doscrkeyb,
  dodflsave,
  donamsave,
  NIL_FUN,
  NIL_FUN,
};

char some_string[32];

static char *menu1[] = {
  "Filenames and paths",
  "File transfer protocols",
  "Serial port setup",
  "Modem and dialing",
  "Screen and keyboard",
  some_string,
  "Save setup as..",
  "Exit",
  "Exit from Minicom",
  MENU_END,
};


int config(setup)
int setup;
{
  int c;
  char *s;

  /* Find out extension of parameter file */
  s = parfile + strlen(LIBDIR) + 8;
  sprintf(some_string, "Save setup as %s", s);

  if (!setup) menu1[8] = MENU_END;

  c = wselect(13, 10, menu1, funcs1, "configuration", stdattr, MFG, MBG);
  if (c == 9) return(1);
  return(0);
}

static char *speeds[] =
   { "300", "1200", "2400", "4800", "9600", "19200", "38400", };

/*
 * Ask user for Baudrate, Bits and Parity
 */
void get_bbp(ba, bi, pa)
char *ba;
char *bi;
char *pa;
{
  int c;
  WIN *w;
  int x, y;

  w = wopen(21, 6, 60, 19, BDOUBLE, stdattr, MFG, MBG, 0, 0, 1);
  wtitle(w, TMID, "Comm Parameters");

  dirflush = 0;

  wlocate(w, 0, 3);
  wputs(w, "   Speed          Parity          Data\n\n");
  wputs(w, " A: 300           H: None         M: 5\n");
  wputs(w, " B: 1200          I: Even         N: 6\n");
  wputs(w, " C: 2400          J: Odd          O: 7\n");
  wputs(w, " D: 4800                          P: 8\n");
  wputs(w, " E: 9600\n");
  wputs(w, " F: 19200         K: 8-N-1\n");
  wputs(w, " G: 38400         L: 7-E-1\n");
  wputs(w, "\n Choice, or <Enter> to exit? ");
  x = w->curx;
  y = w->cury;
  
  bi[1] = 0;
  pa[1] = 0;

  wredraw(w, 1);

  while(1) {
  	wlocate(w, 1, 1);
  	wprintf(w, "Current: %5s %s%s1  ", ba, bi, pa);
  	wlocate(w, x, y);
  	wflush();
  	c = getch();
  	if (c >= 'a') c -= 32;
  	switch(c) {
  		case 'A':
  		case 'B':
  		case 'C':
  		case 'D':
  		case 'E':
  		case 'F':
  		case 'G':
  			strcpy(ba, speeds[c - 'A']);
  			break;
  		case 'H':
  			pa[0] = 'N';
  			break;
  		case 'I':
  			pa[0] = 'E';
  			break;
  		case 'J':
  			pa[0] = 'O';
  			break;
  		case 'K':	
  			pa[0] = 'N';
  			bi[0] = '8';
  			break;
  		case 'L':
  			pa[0] = 'E';
  			bi[0] = '7';
  			break;
  		case 'M':
  			bi[0] = '5';
  			break;
  		case 'N':
  			bi[0] = '6';
  			break;
  		case 'O':
  			bi[0] = '7';
  			break;
  		case 'P':
  			bi[0] = '8';
  			break;
  		case 27:
  		case '\n':
  		case '\r':
  			dirflush = 1;
  			wclose(w, 1);
  			return;
  		default:
  			break;
  	}
  }
}

