/*
 * This file is part of the Minicom Communications Program,
 * written by Miquel van Smoorenburg 1991/1992/1993.
 *
 * main.c - main loop of emulator.
 */
#include <sys/types.h>
#if defined (_POSIX_SOURCE) || defined(_BSD43)
#  include <unistd.h>
#  include <stdlib.h>
#else
   char *getenv();
#endif
#undef NULL
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <setjmp.h>
#include <errno.h>

#include "window.h"
#include "keyboard.h"
#include "minicom.h"
#include "vt100.h"
#include "configsym.h"

static jmp_buf albuf;

char *Version = "@(#)Minicom V1.5B 1993";	/* SCCS ID */
static char *version = "1.5B 1993";	 	/* For status line */

void curs_status();

/*
 * Return the last part of a filename.
 */
char *basename(s)
char *s;
{
  char *p;
  
  if((p = strrchr(s, '/')) == (char *)NULL)
  	p = s;
  else
  	p++;
  return(p);
}

/*
 * Leave.
 */
void leave(s)
char *s;
{
  if (stdwin) wclose(stdwin, 1);
  if (portfd > 0) {
	m_restorestate(portfd);
	close(portfd);
  }
  if (lockfile[0]) unlink(lockfile);
  if (P_CALLIN[0]) (void) fastsystem(P_CALLIN, CNULL, CNULL, CNULL);
  if (real_uid) chown(P_PORT, portuid, portgid);
  fprintf(stderr, "%s", s);
  exit(1);
}

/*
 * Return text describing command-key.
 */
char *esc_key()
{
  static char buf[16];

  if (!alt_override && P_ESCAPE[0] == '^') {
  	sprintf(buf, "CTRL-%c ", P_ESCAPE[1]);
  	return(buf);
  }
#if defined(_MINIX) || defined(_COHERENT) || defined(linux)
  sprintf(buf, "ALT-");
#else
  sprintf(buf, "Meta-");
#endif
  return(buf);
}

static void get_alrm(dummy)
int dummy;
{
  longjmp(albuf, 1);
}

/*
 * Open the terminal.
 */
int open_term(doinit)
int doinit;
{
  struct stat stt;
  char buf[128];
  int fd, n = 0;
  int pid;

  /* First see if the lock file directory is present. */
  if (stat(P_LOCK, &stt) == 0)
  	sprintf(lockfile, "%s/LCK..%s", P_LOCK, basename(P_PORT));
  else
	lockfile[0] = 0;

  if (doinit >= 0 && lockfile[0] && (fd = open(lockfile, O_RDONLY)) >= 0) {
	n = read(fd, buf, 127);
	close(fd);
	if (n > 0) {
		pid = -1;
		if (n == 4)
			/* Kermit-style lockfile. */
			pid = *(int *)buf;
		else {
			/* Ascii lockfile. */
			buf[n] = 0;
			sscanf(buf, "%d", &pid);
		}
		if (pid > 0 && kill(pid, 0) < 0 &&
			errno == ESRCH) {
		    fprintf(stderr, "Lockfile is stale. Overriding it..\n");
		    sleep(1);
		    unlink(lockfile);
		} else
		    n = 0;
	}
	if (n == 0) {
  		if (stdwin != NIL_WIN) wclose(stdwin, 1);
  		fprintf(stderr, "Device %s is locked.\n", P_PORT);
		return(-1);
	}
  }

  if (setjmp(albuf) == 0) {
	portfd = -1;
	signal(SIGALRM, get_alrm);
	alarm(2);
#ifdef O_NDELAY
	portfd = open(P_PORT, O_RDWR|O_NDELAY);
	if (portfd >= 0){
		/* Open it a second time, now with O_NDELAY. */
		if (doinit >= 0) m_savestate(portfd);
		m_setparms(portfd, P_BAUDRATE, P_PARITY, P_BITS);
		fd = portfd;
		portfd = open(P_PORT, O_RDWR);
		if (portfd < 0 && doinit >= 0) m_restorestate(fd);
		close(fd);
	}
#else
	portfd = open(P_PORT, O_RDWR);
	if (portfd >= 0) {
		if (doinit >= 0) m_savestate(portfd);
		m_setparms(portfd, P_BAUDRATE, P_PARITY, P_BITS);
	}
#endif
  }
  alarm(0);
  signal(SIGALRM, SIG_IGN);
  if (portfd < 0) {
	if (doinit >= 0) {
  		if (stdwin != NIL_WIN) wclose(stdwin, 1);
  		fprintf(stderr, "Cannot open %s. Sorry.\n",
  				P_PORT);
		return(-1);
	}
	werror("Cannot open %s!", P_PORT);
	return(-1);
  }
  /* Remember owner of port */
  stat(P_PORT, &stt);
  portuid = stt.st_uid;
  portgid = stt.st_gid;

  /* Give it to us! */
  if (real_uid != 0) chown(P_PORT, real_uid, real_gid);

  if(doinit > 0 && P_CALLOUT[0])
#ifdef _COHERENT
	close(portfd);	
	if(fastsystem(P_CALLOUT, CNULL, CNULL, CNULL) < 0) {
#else
  	if(fastsystem(P_CALLOUT, CNULL, CNULL, CNULL) != 0) {
#endif
  		if (stdwin != NIL_WIN) wclose(stdwin, 1);
  		fprintf(stderr, "Could not setup for dial out.\n");
  		if (lockfile[0]) unlink(lockfile);
		close(portfd);
		return(-1);
  	}
#ifdef _COHERENT
	portfd = open(P_PORT, O_RDWR);
#endif
  if (doinit >= 0 && lockfile[0]) {
  	/* Create lockfile compatible with UUCP-1.2 */
#ifdef _COHERENT
	if ((fd = creat(lockfile, 0666)) < 0) {
#else
  	if ((fd = open(lockfile, O_WRONLY | O_CREAT | O_EXCL, 0666)) < 0) {
#endif
  		if (stdwin != NIL_WIN) wclose(stdwin, 1);
  		fprintf(stderr, "Cannot create lockfile. Sorry.\n");
		return(-1);
  	}
  	sprintf(buf, "%05d minicom %20.20s", getpid(), username);
  	write(fd, buf, strlen(buf));
  	close(fd);
  }
  /* Set CLOCAL mode */
  m_nohang(portfd);
  /* Set Hangup on Close if program crashes. (Hehe) */
  m_hupcl(portfd, 1);
  if (doinit > 0) m_flush(portfd);
  return(0);
}


/* Function to write output. */
static void do_output(s)
char *s;
{
  write(portfd, s, strlen(s));
}

/* Function to handle keypad mode switches. */
static void kb_handler(a, b)
int a,b;
{
  cursormode = b;
  keypadmode = a;
  if (st) curs_status();
}

/*
 * Initialize screen and status line.
 */
void init_emul(type)
int type;
{
  int x = -1, y = -1;
  char attr;
  int size = 80;

  if (st != NIL_WIN) {
  	wclose(st, 1);
  	tempst = 0;
  	st = NIL_WIN;
  }

  if (us != NIL_WIN) {
	x = us->curx; y = us->cury;
	attr = us->attr;
  	wclose(us, 0);
  }	

  /* See if we have space for a status line */
  if (LINES > (24 + (type == MINIX)) && P_STATLINE[0] == 'e') {
	if (COLS - 1 > 80) size = COLS - 1;
  	st = wopen(0, LINES - 1, size, LINES - 1,
  			BNONE, A_REVERSE, WHITE, BLACK, 1, 0, 0);
	wredraw(st, 1);
  }

  /* Open a new window. */
  us = wopen(0, 0, COLS - 1, LINES + (st == NIL_WIN) - 2,
  			BNONE, A_NORMAL, SFG, SBG, 1, 256, 0);

  if (x >= 0) {
  	wlocate(us, x, y);
  	wsetattr(us, attr);
  }

  us->autocr = 0;

#if 0
  /* This is meant to erase the statusline if needed, but
     we clear the whole screen anyway. */

  if (type == MINIX && terminal != MINIX && LINES > 24) {
  	x = us->curx;
  	y = us->cury;
  	wlocate(us, 0, 24);
  	wclreol(us);
  	wlocate(us, x, y);
  }
#endif

  terminal = type;
  lines = LINES - (st != NIL_WIN);
  cols = COLS;
  
  /* Install and reset the terminal emulator. */
  vt_install(do_output, kb_handler, us);
  vt_init(type, SFG, SBG, type != VT100, 0);

  if (st != NIL_WIN) show_status();
}

/*
 * Locate the cursor at the correct position in
 * the user screen.
 */
static void ret_csr()
{
  wlocate(us, us->curx, us->cury);
  wflush();
}

/*
 * Show baudrate, parity etc.
 */
void mode_status()
{
  wlocate(st, 20, 0);
  wprintf(st, " %-5s %s%s1", P_BAUDRATE, P_BITS, P_PARITY);
  ret_csr();
}

/*
 * Show offline or online time.
 * If real dcd is not supported, Online and Offline will be
 * shown in capitals.
 */
void time_status()
{
  wlocate(st, 66, 0);
  if (online < 0)
  	wputs(st, P_HASDCD[0] == 'Y' ? " Offline     " : " OFFLINE     ");
  else
  	wprintf(st," %s %02ld:%02ld", P_HASDCD[0] == 'Y' ? "Online" : "ONLINE",
  		online / 3600, (online / 60) % 60);
  		
  ret_csr();
}

/*
 * Show the mode which the cursor keys are in (normal or applications)
 */
void curs_status()
{
  wlocate(st, 33, 0);
  wprintf(st, cursormode == NORMAL ? "NOR" : "APP");
  ret_csr();
}

/*
 * Update the online time.
 */
static void updtime()
{
  static int old_online = 0;

  if (old_online == online) return;
  old_online = online;
  if (st != NIL_WIN) {
  	time_status();
  	ret_csr();
  }
  wflush();
}

/*
 * Show the status line 
 */
void show_status()
{
  st->direct = 0;
  wlocate(st, 0, 0);
  wprintf(st, " %7.7sZ for help \263           \263     \263 Minicom %-9.9s \263       \263 ",
  	esc_key(), version);
  mode_status();
  time_status();
  curs_status();
  wlocate(st, 59, 0);
  switch(terminal) {
  	case VT100:
  		wputs(st, "VT100");
  		break;
  	case MINIX:
  		wputs(st, "MINIX");
  		break;
  	case ANSI:
  		wputs(st, "ANSI");
  		break;
  }
  wredraw(st, 1);
  ret_csr();
}

/*
 * Show the name of the script running now.
 */
void scriptname(s)
char *s;
{
  if (st == NIL_WIN) return;
  wlocate(st, 39, 0);
  if (*s == 0)
  	wprintf(st, "Minicom %-9.9s", version);
  else
  	wprintf(st, "script %-10.10s", s);
  ret_csr();
}

/*
 * Show status line temporarily
 */
static void showtemp()
{
  if (st != NIL_WIN) return;

  st = wopen(0, LINES - 1, COLS - 1, LINES - 1,
		BNONE, A_REVERSE, WHITE, BLACK, 1, 0, 0);
  show_status();
  tempst = 1;
}

/*
 * The main terminal loop:
 *	- If there are characters recieved send them
 *	  to the screen via the appropriate translate function.
 */
int do_terminal()
{
  char buf[128];
  char *ptr;
  static time_t t1, start;
  int c;
  int dcd_support = P_HASDCD[0] == 'Y';
  int x;
  int blen;
  dirflush = 0;

dirty_goto:
  /* Show off or online time */
  updtime();

  /* If the status line was shown temporarily, delete it again. */
  if (tempst) {
  	tempst = 0;
  	wclose(st, 1);
  	st = NIL_WIN;
  }

  /* Set the terminal modes */
  (void) setcbreak(2); /* Raw, no echo */

  keyboard(KSTART, 0);

  /* Main loop */
  while(1) {
	/* See if window size changed */
	if (size_changed) {
		size_changed = 0;
#if 0
		wclose(us, 0);
		us = NIL_WIN;
		if (st) wclose(st, 0);
		st = NIL_WIN;
		wclose(stdwin, 0);
		if (win_init(SFG, SBG, A_NORMAL) < 0)
			leave("Could not re-initialize window system.");
		init_emul(terminal);
#else
		werror("Resize not supported, screen may be messed up!");
#endif
	}
  	/* See if we're online. */
  	if ((!dcd_support && bogus_dcd) || (dcd_support && m_getdcd(portfd))) {
  		if (online < 0) {
  			time(&start);
  			t1 = start;
  			online = 0;
  			updtime();
  		}
  	} else {
  		online = -1;
  		updtime();
  	}

  	/* Update online time */
  	if (online >= 0) {
  		time(&t1);
  		if (t1 > (online + start + 59)) {
  			online = t1 - start;
  			updtime();
  		}
  	}

	/* Check for I/O or timer. */
	x = check_io(portfd, 0, 1000, buf, &blen);

	/*  Send data from the modem to the screen. */
  	if ((x & 1) == 1) {
  		ptr = buf;
  		while(blen--)
  			vt_out(*ptr++);
  		wflush();
  	}
	
	/* Read from the keyboard and send to modem. */
	if ((x & 2) == 2) {
		/* See which key was pressed. */
		c = keyboard(KGETKEY, 0);

		/* Was this a command key? */
		if ((escape == 128 && c > 128 && c < 256) || c == escape) {
			/* Stop keyserv process if we have it. */
  			keyboard(KSTOP, 0);

  			/* Restore keyboard modes */
  			(void) setcbreak(1); /* Cbreak, no echo */

  			/* Show status line temporarily */
  			showtemp();
  			if (c == escape) /* CTRL A */
				(void) read(0, &c, 1);

  			if (c > 128) c -= 128;
  			if (c > ' ') {
				dirflush = 1;
				m_flush(0);
				return(c);
			}
			/* CTRLA - CTRLA means send one CTRLA */
			write(portfd, &c, 1);
			goto dirty_goto;
  		}

		/* No, just a key to be sent. */
		vt_send(c);
	}
  }
}
