/*
 * This file is part of the Minicom Communications Program,
 * written by Miquel van Smoorenburg 1991/1992/1993.
 *
 * sysdep2.c - system dependant routines
 *
 * getrowcols	- get number of columns and rows from the environment.
 * setcbreak	- set tty mode to raw, cbreak or normal.
 * enab_sig	- enable / disable tty driver signals.
 * strtok	- for systems that don't have it.
 * dup2		- for ancient systems like SVR2.
 */
#include <sys/types.h>
#if defined (_POSIX_SOURCE) || defined(_BSD43)
#  include <stdlib.h>
#  include <unistd.h>
#else
extern char *getenv();
#endif
#ifdef _NO_TERMIOS
#  undef _POSIX_SOURCE
#endif
#if defined (_V7) && !defined(_POSIX_SOURCE)
#  include <sgtty.h>
#endif
#if defined (_SYSV) && !defined(_POSIX_SOURCE)
#  include <termio.h>
#endif
#ifdef _POSIX_SOURCE
#  include <termios.h>
#endif
#if defined(_BSD43) || defined (_SYSV)
#  include <sys/ioctl.h>
#endif

/* Some ancient SysV systems don't define these */
#ifndef VMIN
#  define VMIN 4
#endif
#ifndef VTIME
#  define VTIME 5
#endif
#ifndef IUCLC
#  define IUCLC 0
#endif
#ifndef IXANY
#  define IXANY 0
#endif

/* If this is SysV without Posix, emulate Posix. */
#if defined(_SYSV) && !defined(_POSIX_SOURCE)
#  define termio termios
#  define _POSIX_SOURCE
#  ifndef TCSANOW
#    define TCSANOW   0
#    define TCSADRAIN 1
#  endif
#  define tcgetattr(fd, tty)        ioctl(fd, TCGETA, tty)
#  define tcsetattr(fd, flags, tty) ioctl(fd, \
				    (flags == TCSANOW) ? TCSETA : TCSETAW, tty)
#  define tcsendbreak(fd, len)      ioctl(fd, TCSBRK, 0)
#endif

/* Get the number of rows and columns for this screen. */
void getrowcols(rows, cols)
int *rows;
int *cols;
{
#ifdef TIOCGWINSZ
	struct winsize ws;

	if (ioctl(0, TIOCGWINSZ, &ws) < 0) {
		*rows = 0;
		*cols = 0;
	} else {
		*rows = ws.ws_row;
		*cols = ws.ws_col;
	}	
#else
#  ifdef TIOCGSIZE
	struct ttysize ws;

	if (ioctl(0, TIOCGSIZE, &ws) < 0) {
		*rows = 0;
		*cols = 0;
	} else {
		*rows = ws.ts_lines;
		*cols = ws.ts_cols;
	}
#  else
	char *p, *getenv();

	if (p = getenv("LINES"))
		*rows = atoi(p);
	else
		*rows = 0;
	if (p = getenv("COLUMNS"))
		*cols = atoi(p);
	else
		*cols = 0;
#  endif
#endif	
}

/*
 * Set cbreak mode.
 * Mode 0 = normal.
 * Mode 1 = cbreak, no echo
 * Mode 2 = raw, no echo.
 * Mode 3 = only return erasechar (for wkeys.c)
 *
 * Returns: the current erase character.
 */  
int setcbreak(mode)
int mode;
{
#ifdef _POSIX_SOURCE
  struct termios tty;
  static int init = 0;
  static int erasechar;
  static struct termios saveterm;
#ifdef _HPUX_SOURCE
  static int lastmode = -1;
#endif

#ifndef XCASE
#  ifdef _XCASE
#    define XCASE _XCASE
#  else
#    define XCASE 0
#  endif
#endif

  if (init == 0) {
	tcgetattr(0, &saveterm);
	erasechar = saveterm.c_cc[VERASE];
	init++;
  }

  if (mode == 3) return(erasechar);

#ifdef _HPUX_SOURCE
  /* In HP/UX, TCSADRAIN does not work for me. So we use only RAW
   * or NORMAL mode, so we never have to switch from cbreak <--> raw
   */
  if (mode == 1) mode = 2;
#endif

  /* Avoid overhead */
#ifdef _HPUX_SOURCE
  if (mode == lastmode) return(erasechar);
  lastmode = mode;
#endif

  /* Always return to default settings first */
  tcsetattr(0, TCSADRAIN, &saveterm);

  if (mode == 0) {
  	return(erasechar);
  }

  tcgetattr(0, &tty);
  if (mode == 1) {
	tty.c_oflag &= ~OPOST;
	tty.c_lflag &= ~(XCASE|ECHONL|NOFLSH);
  	tty.c_lflag &= ~(ICANON | ISIG | ECHO);
	tty.c_iflag &= ~(ICRNL|INLCR);
  	tty.c_cflag |= CREAD;
  	tty.c_cc[VTIME] = 5;
  	tty.c_cc[VMIN] = 1;
  }
  if (mode == 2) { /* raw */
  	tty.c_iflag &= ~(IGNBRK | IGNCR | INLCR | ICRNL | IUCLC | 
  		IXANY | IXON | IXOFF | INPCK | ISTRIP);
  	tty.c_iflag |= (BRKINT | IGNPAR);
	tty.c_oflag &= ~OPOST;
	tty.c_lflag &= ~(XCASE|ECHONL|NOFLSH);
  	tty.c_lflag &= ~(ICANON | ISIG | ECHO);
  	tty.c_cflag |= CREAD;
  	tty.c_cc[VTIME] = 5;
  	tty.c_cc[VMIN] = 1;
  }	
  tcsetattr(0, TCSADRAIN, &tty);
  return(erasechar);
#else
  struct sgttyb args;
  static int init = 0;
  static int erasechar;
  static struct sgttyb sgttyb;
  static struct tchars tchars;
#ifdef _BSD43  
  static struct ltchars ltchars;
#endif
  
  if (init == 0) {
	(void) ioctl(0, TIOCGETP, &sgttyb);
	(void) ioctl(0, TIOCGETC, &tchars);
#ifdef _BSD43
	(void) ioctl(0, TIOCGLTC, &ltchars);
#endif
	erasechar = sgttyb.sg_erase;
	init++;
  }

  if (mode == 3) return(erasechar);

  if (mode == 0) {
  	(void) ioctl(0, TIOCSETP, &sgttyb);
	(void) ioctl(0, TIOCSETC, &tchars);
#ifdef _BSD43
	(void) ioctl(0, TIOCSLTC, &ltchars);
#endif
  	return(erasechar);
  }

  (void) ioctl(0, TIOCGETP, &args);
  if (mode == 1) {
	args.sg_flags |= CBREAK;
	args.sg_flags &= ~(ECHO|RAW);
  }
  if (mode == 2) {
  	args.sg_flags |= RAW;
  	args.sg_flags &= ~(ECHO|CBREAK);
  }
  (void) ioctl(0, TIOCSETP, &args);
  return(erasechar);
#endif

}

/* Enable / disable signals from tty driver */
void enab_sig(onoff)
int onoff;
{
#ifdef _POSIX_SOURCE
  struct termios tty;
  
  tcgetattr(0, &tty);
  if (onoff)
	tty.c_lflag |= ISIG;
  else
	tty.c_lflag &= ~ISIG;
  tcsetattr(0, TCSADRAIN, &tty);
#endif
}

#if 0
/*
 * strtok taken from the Minix library - I suppose this one's PD.
 *
 * Get next token from string s (NULL on 2nd, 3rd, etc. calls),
 * where tokens are nonempty strings separated by runs of
 * chars from delim.  Writes NULs into s to end tokens.  delim need not
 * remain constant from call to call.
 */

char *strtok(s, delim)		/* NULL if no token left */
char *s;
register char *delim;
{
  register char *scan;
  char *tok;
  register char *dscan;

  if (s == (char *)0 && scanpoint == (char *)0) return((char *)0);
  if (s != (char *)0)
	scan = s;
  else
	scan = scanpoint;

  /* Scan leading delimiters. */
  for (; *scan != '\0'; scan++) {
	for (dscan = delim; *dscan != '\0'; dscan++)
		if (*scan == *dscan) break;
	if (*dscan == '\0') break;
  }
  if (*scan == '\0') {
	scanpoint = (char *)0;
	return((char *)0);
  }
  tok = scan;

  /* Scan token. */
  for (; *scan != '\0'; scan++) {
	for (dscan = delim; *dscan != '\0';)	/* ++ moved down. */
		if (*scan == *dscan++) {
			scanpoint = scan + 1;
			*scan = '\0';
			return(tok);
		}
  }

  /* Reached end of string. */
  scanpoint = (char *)0;
  return(tok);
}
#endif

#ifdef _SVR2
/* Fake the dup2() system call */
int dup2(from, to)
int from, to;
{
  int files[20];
  int n, f, exstat = -1;
  extern int errno;

  /* Ignore if the same */
  if (from == to) return(to);

  /* Initialize file descriptor table */
  for(f = 0; f < 20; f++) files[f] = 0;

  /* Close "to" file descriptor, if open */
  close(to);

  /* Keep opening files until we reach "to" */
  while((n = open("/dev/null", 0)) < to && n >= 0) {
  	if (n == from) break;
  	files[n] = 1;
  }
  if (n == to) {
  	/* Close "to" again, and perform dup() */
  	close(n);
  	exstat = dup(from);
  } else {
  	/* We failed. Set exit status and errno. */
  	if (n > 0) close(n);
  	exstat = -1;
  	errno = EBADF;
  }
  /* Close all temporarily opened file descriptors */
  for(f = 0; f < 20; f++) if (files[f]) close(f);

  /* We're done. */
  return(exstat);
}
#endif

