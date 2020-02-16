/*
 * This file is part of the Minicom Communications Program,
 * written by Miquel van Smoorenburg 1991/1992/1993.
 *
 * Read a keypress from the standard input. If it is an escape
 * code, return a special value.
 *
 * WARNING: possibly the most ugly code in this package!
 */
#if defined(_BSD43) && defined(_SELECT)
#  undef _POSIX_SOURCE
#endif
#include <sys/types.h>
#if defined(MINIX) || defined(linux)
#  include <termcap.h>
#else
char *tgetstr();
int tgetent();
#endif
#if defined (_POSIX_SOURCE) || defined(_BSD43)
#  include <stdlib.h>
#  include <unistd.h>
#else
char *getenv();
#endif
#ifdef _SELECT
#  include <sys/time.h>
#else
#  include <signal.h>
#endif
#include <string.h>
#include <errno.h>
#include "window.h"
#ifndef BBS
#  include "config.h"
#endif

#if KEY_KLUDGE && defined(linux)
#  include <sys/kd.h>
#  include <sys/ioctl.h>
#endif

static struct key _keys[NUM_KEYS];
extern int setcbreak();
extern WIN *us;

/*
 * The following is an external pointer to the termcap info.
 * If it's NOT zero then the main program has already
 * read the termcap for us. No sense in doing it twice.
 */
extern char *_tptr;

static char erasechar;
static int gotalrm;
extern int errno;
int pendingkeys = 0;

#ifdef _MINIX
/*
 * MINIX (and some others, sigh) use F1-10. BSD uses k1-a (mostly..)
 */
static char *func_key[] = { 
	"", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F0",
	"kh", "kP", "ku", "kl", "kr", "kd", "kH", "kN", "kI", "kD",
	(char *)0 };
#else
static char *func_key[] = { 
	"", "k1", "k2", "k3", "k4", "k5", "k6", "k7", "k8", "k9", "ka",
	"kh", "kP", "ku", "kl", "kr", "kd", "kH", "kN", "kI", "kD",
	(char *)0 };
#endif	

#if KEY_KLUDGE
#ifdef _V7
#  include <sgtty.h>
#endif
/*
 * A VERY DIRTY HACK FOLLOWS:
 * This routine figures out if the tty we're using is a serial
 * device OR an IBM PC console. If we're using a console, we can
 * easily reckognize single escape-keys since escape sequences
 * always return > 1 characters from a read()
 */
static int isconsole;
 
static int testconsole()
{
#ifndef linux
  struct sgttyb sg, ng;

  /* Get parameters */
  ioctl(0, TIOCGETP, &ng);
  /* Save old parameters */
  ioctl(0, TIOCGETP, &sg);
  
  ng.sg_ispeed = 0;
  ng.sg_ospeed = 1;
  
  /* Set new speed */
  ioctl(0, TIOCSETP, &ng);
  /* Read new speed */
  ioctl(0, TIOCGETP, &ng);
  /* Restore old value */
  ioctl(0, TIOCSETP, &sg);
  
  /* RS 232 lines will have defaulted the ispeed since it was too low! */
  return (ng.sg_ispeed == 0);
#else
  /* For Linux it's easy to see if this is a VC. */
  int info;

  return( ioctl(0, KDGETLED, &info) == 0);
#endif
}

/*
 * Function to read chunks of data from fd 0 all at once
 */
static int keys_in_buf;

static int cread(c)
char *c;
{
  static char buf[32];
  static int idx = 0;
  static int lastread = 0;

  if (idx > 0 && idx < lastread) {
  	*c = buf[idx++];
	keys_in_buf--;
  	return(lastread);
  }
  idx = 0;
  do {
	lastread = read(0, buf, 32);
	keys_in_buf = lastread - 1;
  } while(lastread < 0 && errno == EINTR);

  *c = buf[0];
  if (lastread > 1) idx = 1;
  return(lastread);
}
#endif

static void _initkeys()
{
  int i;
  static char *cbuf, *tbuf;
  char *term;

  if (_tptr == CNULL) {
	if ((tbuf = (char *)malloc(512)) == CNULL || 
		(cbuf = (char *)malloc(1024)) == CNULL) {
  		write(2, "Out of memory.\n", 15);
  		exit(1);
	}
	term = getenv("TERM");
	switch(tgetent(cbuf, term)) {
  		case 0:
  			write(2, "No termcap entry.\n", 18);
  			exit(1);
  		case -1:
  			write(2, "No /etc/termcap present!\n", 25);
  			exit(1);
  		default:
  			break;
  	}
	_tptr = tbuf;
  }	
/* Initialize codes for special keys */
  for(i = 0; func_key[i]; i++) {
  	if ((_keys[i].cap = tgetstr(func_key[i], &_tptr)) == CNULL)
  		_keys[i].cap = "";
  	_keys[i].len = strlen(_keys[i].cap);
  }
#if KEY_KLUDGE
  isconsole = testconsole();
#endif
}
  
/*
 * Dummy routine for the alarm signal
 */
#ifndef _SELECT
static void dummy()
{
  gotalrm = 1;
}
#endif
  
/*
 * Read a character from the keyboard.
 * Handle special characters too!
 */
int getch()
{
  int f, g;
  int match = 1;
  int len;
  unsigned char c;
  static unsigned char mem[8];
  static int leftmem = 0;
  static int init = 0;
  int nfound = 0;
#ifdef _SELECT
  struct timeval timeout;
  fd_set readfds;
  static fd_set *nofds = (fd_set *)0;
#endif

  if (init == 0) {
  	_initkeys();
  	init++;
  	erasechar = setcbreak(3);
  }

  /* Some sequence still in memory ? */
  if (leftmem) {
	leftmem--;
	if (leftmem == 0) pendingkeys = 0;
	return(mem[leftmem]);
  }
  gotalrm = 0;
  pendingkeys = 0;

  for (len = 1; len < 8 && match; len++) {
#ifdef _SELECT
#if KEY_KLUDGE
	if (len > 1 && keys_in_buf == 0) {
#else
	if (len > 1) {
#endif
		timeout.tv_sec = 0;
		timeout.tv_usec = 400000; /* 400 ms */
#ifdef FD_SET
		FD_ZERO(&readfds);
		FD_SET(0, &readfds);
#else
		readfs = 1; /* First bit means file descriptor #0 */
#endif
#ifdef _HPUX_SOURCE
		/* HPUX prototype of select is mangled */
		nfound = select(1, (int *)&readfds,
				(int *)nofds, (int *)nofds, &timeout);
#else
		nfound = select(1, &readfds, nofds, nofds, &timeout);
#endif
		if (nfound == 0) {
			break;
		}
	}
#else
	if (len > 1) {
		signal(SIGALRM, dummy);
		alarm(1);
	}
#endif
#if KEY_KLUDGE
	while((nfound = cread(&c)) < 0 && (errno == EINTR && !gotalrm))
		;
#else
  	while ((nfound = read(0, &c, 1)) < 0 && (errno == EINTR && !gotalrm))
  		;
#endif
#ifndef _SELECT
	if (len > 1) alarm(0);
#endif
	if (nfound < 1) break;

  	if (len == 1) {
  	/* Enter and erase have precedence over anything else */
 	 	if (c == (unsigned char)'\n')
  			return c;
		if (c == (unsigned char)erasechar)
			return K_ERA;
  	}
#if KEY_KLUDGE
	if (isconsole && nfound == 1 && len == 1) return(c);
#endif
  	mem[len - 1] = c;
  	match = 0;
  	for (f = 0; f < NUM_KEYS; f++)
  	    if (_keys[f].len >= len && strncmp(_keys[f].cap, (char *)mem, len) == 0){
  			match++;
  			if (_keys[f].len == len) {
  				return(f + KEY_OFFS);
  			}
  		}
  }
  /* No match. in len we have the number of characters + 1 */
  len--; /* for convenience */
  if (len == 1) return(mem[0]);
  /* Remember there are more keys waiting in the buffer */
  pendingkeys++;

#ifndef _SELECT
  /* Pressing eg escape twice means escape */
  if (len == 2 && mem[0] == mem[1]) return(mem[0]);
#endif
  
  /* Reverse the "mem" array */
  for(f = 0; f < len / 2; f++) {
  	g = mem[f];
  	mem[f] = mem[len - f - 1];
  	mem[len - f - 1] = g;
  }
  leftmem = len - 1;
  return(mem[leftmem]);
}

