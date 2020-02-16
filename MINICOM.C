/*
 * This file is part of the Minicom Communications Program,
 * written by Miquel van Smoorenburg 1991/1992/1993.
 *
 * minicom.c - main program.
 */
#include <sys/types.h>
#if defined (_POSIX_SOURCE) || defined(_BSD43)
#  include <unistd.h>
#  include <stdlib.h>
#else
   char *getenv();
#endif
#undef NULL
#include <signal.h>
#include <fcntl.h>
#include <setjmp.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <pwd.h>
#include <ctype.h>

#include "window.h"
#include "keyboard.h"
#include "vt100.h"
#define EXTERN
#include "minicom.h"
#include "configsym.h"

#ifndef _NSIG
#  define _NSIG NSIG
#endif

#define RESET 1
#define NORESET 2

#ifdef _SVR2
extern struct passwd *getpwuid();
#endif

#ifdef DEBUG
/* Show signals when debug is on. */
static void signore(sig)
int sig;
{
  if (stdwin != NIL_WIN)
	werror("Got signal %d", sig);
  else
	printf("Got signal %d\r\n", sig);
}
#endif

/*
 * Sub - menu's.
 */

static char *c1[] = { "   Yes  ", "   No   ", CNULL };
static char *c2[] = { "  Close ", " Pause  ", "  Exit  ", CNULL };
static char *c3[] = { "  Close ", " Unpause", "  Exit  ", CNULL };
static char *c7[] = { "   Yes  ", "   No   ", CNULL };
static char *c8[] = { " vt100", " Minix", " Ansi", CNULL };

static void do_hang(askit)
int askit;
{
  int c = 0;
 
  if (askit) c = ask("Hang-up line?", c7);
  if (c == 0) hangup();
}

/*
 * We've got the hangup or term signal.
 */
static void hangsig(sig)
int sig;
{
#if DEBUG
  char txt[80];
#else
  char *txt = "\n"; /* "Thanks for using Minicom\n"; */
  		    /* Allright Will, this is probably overdone :-) */
#endif
  
  werror("Killed by signal %d !\n", sig);
  if (capfp != (FILE *)0) fclose(capfp);
  keyboard(KUNINSTALL, 0);
  hangup();
  modemreset();
  leave(txt);
}

/*
 * Jump to a shell
 */
static void shjump()
{
  char *sh;
  int pid;
  int status;
  int f;

  sh = getenv("SHELL");
  if (sh == CNULL) {
  	werror("SHELL variable not set");
  	return;
  }
  if ((pid = fork()) == -1) {
  	werror("Out of memory: could not fork()");
  	return;
  }
  if (pid != 0) wleave();
  if (pid == 0) {
	for(f = 1; f < _NSIG; f++) signal(f, SIG_DFL);
  	for(f = 3; f < 20; f++) close(f);
	setgid(real_gid);
  	setuid(real_uid);
  	execl(sh, sh, CNULL);
  	exit(1);
  }
  (void) m_wait(&status);
  wreturn();
}

#if HISTORY
/* Get a line from either window or scroll back buffer. */
static ELM *getline(w, no)
WIN *w;
int no;
{
  int i;

  if (no < us->histlines) {
	i = no + us->histline - 1;
	if (i >= us->histlines) i -= us->histlines;
	if (i < 0) i = us->histlines - 1;
	return(us->histbuf + (i * us->xs));
  }

  no -= us->histlines;
  if (no >= w->ys) no = w->ys - 1;
  return(w->map + (no * us->xs));
}

/* Redraw the window. */
static void drawhist(w, y, r)
WIN *w;
int y;
int r;
{
  int f;

  w->direct = 0;
  for(f = 0; f < w->ys; f++)
	wdrawelm(w, f, getline(w, y++));
  if (r) wredraw(w, 1);
  w->direct = 1;
}

/* Scroll back */
static void scrollback()
{
  int y;
  WIN *b_us, *b_st;
  int c;
  char hline[128];

  /* Find out how big a window we must open. */
  y = us->y2;
  if (st == (WIN *)0 || (st && tempst)) y--;

  /* Open a window. */ 
  b_us = wopen(0, 0, us->x2, y, 0, us->attr, COLFG(us->color),
		COLBG(us->color), 0, 0, 0);
  wcursor(b_us, CNONE);

  /* Open a help line window. */
  b_st = wopen(0, y+1, us->x2, y+1, 0, A_REVERSE, WHITE, BLACK, 0, 0, 0);
  b_st->doscroll = 0;
  b_st->wrap = 0;

  /* Make sure help line is as wide as window. */
  strcpy(hline, "   SCROLL MODE    U=up D=down F=page-forward B=page-backward ESC=exit                    ");
  if (b_st->xs < 127) hline[b_st->xs] = 0;
  wprintf(b_st, hline);
  wredraw(b_st, 1);
  wflush();

  /* And do the job. */
  y = us->histlines;

  drawhist(b_us, y, 0);

  while((c = getch()) != K_ESC) {
	switch(c) {
	  case 'u':
	  case K_UP:
		if (y <= 0) break;
		y--;
		wscroll(b_us, S_DOWN);
		wdrawelm(b_us, 0, getline(b_us, y));
		wflush();
		break;
	  case 'd':
	  case K_DN:
		if (y >= us->histlines) break;
		y++;
		wscroll(b_us, S_UP);
		wdrawelm(b_us, b_us->ys - 1, getline(b_us, y + b_us->ys - 1));
		wflush();
		break;
	  case 'b':
	  case K_PGUP:
		if (y <= 0) break;
		y -= b_us->ys;
		if (y < 0) y = 0;
		drawhist(b_us, y, 1);
		break;
	  case 'f':
	  case K_PGDN:
		if (y >= us->histlines) break;
		y += b_us->ys;
		if (y > us->histlines) y = us->histlines;
		drawhist(b_us, y, 1);
		break;
	}
  }
  /* Cleanup. */
  wclose(b_us, y == us->histlines ? 0 : 1);
  wclose(b_st, 1);
  wlocate(us, us->curx, us->cury);
  wflush();
}
#endif

#ifdef SIGWINCH
/* The window size has changed. Re-initialize. */
static void change_size(sig)
int sig;
{
  (void)sig;
  size_changed = 1;
  signal(SIGWINCH, change_size);
}
#endif

/*
 * Read a word from strings 's' and advance pointer.
 */
static char *getword(s)
char **s;
{
  char *begin;

  /* Skip space */
  while(**s == ' ' || **s == '\t') (*s)++;
  /* End of line? */
  if (**s == '\0' || **s == '\n') return((char *)0);
  
  begin = *s;
  /* Skip word */
  while(**s != ' ' && **s != '\t' && **s != '\n' && **s) (*s)++;
  /* End word with '\0' */
  if (**s) {
  	**s = 0;
  	(*s)++;
  }
  return(begin);
}

static char *bletch = 
  "Usage: minicom [-soml] [-c on|off] [-a on|off] [-t TERM] [configuration]\n";

static void usage(env_args, optind, mc)
int env_args, optind;
char *mc;
{
  if (env_args >= optind && mc)
  	fprintf(stderr, "Wrong option in environment MINICOM=%s\n", mc);
  fprintf(stderr, bletch);
  fprintf(stderr, "Type \"minicom -h\" for help.\n");
  exit(1);
}

/* Give some help information */
static void helpthem()
{
  char *mc = getenv("MINICOM");

  printf("\n%s", Version);
#ifdef __DATE__
  printf(" (compiled %s)", __DATE__);
#endif
  printf("\n\n%s\n", bletch);
  printf("  -s             : enter setup mode (only as root)\n");
  printf("  -o             : do not initialize modem & lockfiles at startup\n");
  printf("  -m             : use meta or alt key for commands\n");
  printf("  -l             : literal ; do not translate characters < 32 or > 127\n");
  printf("  -c [on | off]  : ANSI style color usage on or off\n");
  printf("  -a [on | off]  : use reverse or highlight attributes on or off\n");
  printf("  -t term        : override TERM environment variable\n");
  printf("  configuration  : configuration file to use\n\n");
  printf("These options can also be specified in the MINICOM environment variable.\n");
  printf("This variable is currently %s%s.\n", mc ? "set to " : "unset",
	mc ? mc : "");
  printf("\nThe LIBDIR to find the configuration files and the\n");
  printf("access file minicom.users is compiled as %s.\n\n", LIBDIR);
}

int main(argc, argv)
int argc;
char **argv;
{
  int c;			/* Command character */
  int quit = 0;			/* 'q' or 'x' pressed */
  char *s, *bufp;		/* Scratch pointers */
  int dosetup = 0, doinit = 1;	/* -o and -s options */
  char buf[80];			/* Keyboard input buffer */
  char capname[128];		/* Name of capture file */
  struct passwd *pwd;		/* To look up user name */
  int userok = 0;		/* Scratch variables */
  FILE *fp;			/* Scratch file pointer */
  char userfile[256];		/* Locate user file */
  char *use_port;		/* Name of initialization file */
  char *args[20];		/* New argv pointer */
  int argk = 1;			/* New argc */
  extern int getopt(), optind;
  extern char *optarg;		/* From getopt (3) package */
  char *mc;			/* For 'MINICOM' env. variable */
  int env_args;			/* Number of args in env. variable */

  /* Initialize global variables */
  portfd = -1;
  capfp = (FILE *)NULL;
  docap = 0;
  online = linewrap = -1;
  stdattr = A_NORMAL;
  us = NIL_WIN;
  addlf = 0;
  local_echo = 0;
  strcpy(capname, "minicom.cap");
  lockfile[0] = 0;
  tempst = 0;
  st = NIL_WIN;
  us = NIL_WIN;
  bogus_dcd = 0;
  usecolor = 0;
  literal = 0;
  useattr = 1;
  strcpy(termtype, getenv("TERM") ? getenv("TERM") : "dumb");
  stdattr = A_NORMAL;
  use_port = "dfl";
  alt_override = 0;
  scr_name[0] = 0;
  scr_user[0] = 0;
  scr_passwd[0] = 0;
  dial_name = (char *)NULL;
  dial_number = (char *)NULL;
  size_changed = 0;
  escape = 1;
#ifndef DEBUG
  real_uid = getuid();
  real_gid = getgid();
#else
  real_uid = 0;
  real_gid = 0;
#endif

  /* Before processing the options, first add options
   * from the environment variable 'MINICOM'.
   */
  args[0] = "minicom";
  if ((mc = getenv("MINICOM")) != CNULL) {
 	strncpy(buf, mc, 80);
 	bufp = buf;
 	buf[79] = 0;
 	while(isspace(*bufp)) bufp++;
 	while(*bufp) {
 		for(s = bufp; !isspace(*bufp) && *bufp; bufp++)
 			;
 		args[argk++] = s;
 		while(isspace(*bufp)) *bufp++ = 0;
 	}
  }
  env_args = argk;

  /* Add command - line options */
  for(c = 1; c < argc; c++) args[argk++] = argv[c];
  args[argk] = CNULL;

  do {
	/* Process options with getopt */
	while((c = getopt(argk, args, "hlsomc:a:t:")) != EOF) switch(c) {
  		case 's': /* setup */
  			if (real_uid != 0) {
		fprintf(stderr, "minicom: -s switch needs root privilige\n");
				exit(1);
			}
  			dosetup = 1;
  			break;
		case 'h':
			helpthem();
			exit(1);
			break;
  		case 'm': /* metakey */
  			alt_override++;
  			break;
		case 'l': /* Literal ANSI chars */
			literal++;
			break;
		case 't': /* Terminal type */
			strcpy(termtype, optarg);
			break;
  		case 'o': /* DON'T initialize */
 	 		doinit = 0;
  			break;
  		case 'c': /* Color on/off */
  			if (strcmp("on", optarg) == 0) {
  				usecolor = 1;
  				stdattr = A_BOLD;
  				break;
  			}
  			if (strcmp("off", optarg) == 0) {
  				usecolor = 0;
  				stdattr = A_NORMAL;
  				break;
  			}
  			usage(env_args, optind - 1, mc);
  			break;
  		case 'a': /* Attributes on/off */
  			if (strcmp("on", optarg) == 0) {
  				useattr = 1;
  				break;
  			}
  			if (strcmp("off", optarg) == 0) {
  				useattr = 0;
  				break;
  			}
  			usage(env_args, optind - 1, mc);
  			break;
  		default:
  			usage(env_args, optind, mc);
  			break;
  	}

  	/* Now, get portname if mentioned. Stop at end or '-'. */
 	while(optind < argk && args[optind][0] != '-')
  		use_port = args[optind++];

    /* Loop again if more options */
  } while(optind < argk);

  if (real_uid == 0 && dosetup == 0) {
	fprintf(stderr, "%s%s%s",
  "minicom: WARNING: please don't run minicom as root when not maintaining\n",
  "                  it (with the -s switch) since all changes to the\n",
  "                  configuration will be GLOBAL !.\n");
	sleep(5);
  }

  /* Avoid fraude ! */	
  for(s = use_port; *s; s++) if (*s == '/') *s = '_';
  sprintf(parfile, "%s/minirc.%s", LIBDIR, use_port);

  /* Get password file information of this user. */
  if ((pwd = getpwuid(real_uid)) == (struct passwd *)0) {
  	fprintf(stderr, "You don't exist. Go away.\n");
  	exit(1);
  }

  /* Remember home directory and username. */
  if ((s = getenv("HOME")) == CNULL)
	strcpy(homedir, pwd->pw_dir);
  else
	strcpy(homedir, s);
  strcpy(username, pwd->pw_name);

  /* Get personal parameter file */
  sprintf(pparfile, "%s/.minirc.%s", homedir, use_port);

  /* Check this user in the USERFILE */
  if (real_uid != 0) {
  	sprintf(userfile, "%s/minicom.users", LIBDIR);
	if ((fp = fopen(userfile, "r")) != (FILE *)0) {
		while(fgets(buf, 70, fp) != CNULL && !userok) {
			/* Read first word */
			bufp = buf;
			s = getword(&bufp);
			/* See if the "use_port" matches */
			if (s && !strcmp(pwd->pw_name, s)) {
				if ((s = getword(&bufp)) == CNULL)
					userok = 1;
				else do {
					if (!strcmp(s, use_port)) {
						userok = 1;
						break;
					}
				} while((s = getword(&bufp)) != CNULL);
			}
		}
		fclose(fp);
		if (!userok) {
			fprintf(stderr,
   "Sorry %s. You are not allowed to use configuration %s.\n",
				pwd->pw_name, use_port);
			exit(1);
		}
	}
  }
  buf[0] = 0;

  read_parms();
  stdwin = NIL_WIN; /* It better be! */
  if (!dosetup && open_term(doinit) < 0) exit(1);

#ifdef _SECURE
  /* After open /dev/tty..., we can run setgid uucp instead of setuid root.
   * We have to run setgid uucp to be able to create lockfiles, and
   * to execute the callin and callout programs.
   * To do this, minicom should be installed setuid root/setgid uucp.
   */
  if (real_uid != 0 && geteuid() == 0 && getegid() != real_gid)
  	setuid(real_uid);
#endif

  mc_setenv("TERM", termtype);

  if (win_init(SFG, SBG, A_NORMAL) < 0) leave("");

  if (COLS < 40 || LINES < 10) {
  	leave("Sorry. Your screen is too small.\n");
  }

  if (dosetup) {
  	if (config(1)) {
  		wclose(stdwin, 1);
  		exit(0);
  	}
  	if (open_term(doinit) < 0) exit(1);
  }

  /* Signal handling */
  for(c = 1; c <= _NSIG; c++) {
#ifdef SIGCLD /* Better not mess with those */
	if (c == SIGCLD) continue;
#endif
#ifdef SIGCHLD
	if (c == SIGCHLD) continue;
#endif
	signal(c, hangsig);
  }

  signal(SIGHUP, SIG_IGN);
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);

#ifdef SIGTSTP
  signal(SIGTSTP, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);
#endif
#ifdef SIGTINT
	signal(SIGTINT, SIG_IGN);
#endif
#ifdef SIGWINCH
  signal(SIGWINCH, change_size);
#endif

#if DEBUG
  for(c = 1; c < _NSIG; c++) {
	if (c == SIGTERM) continue; /* Saviour when hung */
	signal(c, signore);
  }
#endif

  keyboard(KINSTALL, 0);

  if (strcmp(P_BACKSPACE, "BS") != 0) 
	keyboard(KSETBS, P_BACKSPACE[0] == 'B' ? 8 : 127);
  if (alt_override)
	keyboard(KSETESC, 128);
  else if (strcmp(P_ESCAPE, "^A") != 0)
	keyboard(KSETESC, P_ESCAPE[0] == '^' ? P_ESCAPE[1] & 31 : 128);

  st = NIL_WIN;
  us = NIL_WIN;

  init_emul(VT100);
 
  if (doinit) modeminit();
 
  wputs(us, "\nMinicom 1.5 Beta Copyright (c) 1993 Miquel van Smoorenburg\r\n");
  wprintf(us, "\nPress %sZ for help on special keys\r\n\n", esc_key());

  readdialdir();

  /* The main loop calls do_terminal and gets a function key back. */
  do {
	c = do_terminal();
dirty_goto:	
	switch(c + 32 *(c >= 'A' && c <= 'Z')) {
		case 'a': /* Add line feed */
			addlf = !addlf;
			vt_set(addlf, -1, NULL, -1, -1, -1, -1);
			s = addlf ? "Add linefeed ON" : "Add linefeed OFF";
			werror(s);
			break;
#if _SELECT
		case 'e': /* Local echo on/off. */
			local_echo = !local_echo;
			vt_set(-1, -1, NULL, -1, -1, local_echo, -1);
			s = local_echo ? "Local echo ON" : "Local echo OFF";
			werror(s);
			break;
#endif
		case 'z': /* Help */
			c = help();
			if (c != 'z') goto dirty_goto;
			break;
		case 'c': /* Clear screen */
			winclr(us);
			break;	
		case 'f': /* Send break */
			sendbreak();
			break;
#if HISTORY
		case 'b': /* Scroll back */
			scrollback();
			break;
#endif
		case 'm': /* Initialize modem */
			modeminit();
			break;	
		case 'q': /* Exit without resetting */
			c = ask("Leave without reset?", c1);
			if (c == 0) quit = NORESET;
			break;
		case 'x': /* Exit Minicom */
			c = ask("Leave Minicom?", c1);
			if (c == 0) {
				quit = RESET;
				if(online >= 0)
					do_hang(0);
				modemreset();	 
			}
			break;
		case 'l': /* Capture file */
			if (capfp == (FILE *)0 && !docap) {
				s = input("Capture to which file? ", capname);
				if (s == CNULL || *s == 0) break;
				if ((c = waccess(s)) < 0 ||
				    (capfp = fopen(s, "a")) == (FILE *)NULL) {
					werror("Cannot open capture file");
					break;
				} else if (c == A_OK_NOTEXIST)
#ifndef DEBUG
#ifndef HAS_FCHOWN
						/* FIXME: should use fchown */
						chown(s, real_uid, real_gid);
#else
						fchown(fileno(capfp), real_uid,
								real_gid);
#endif
#endif
				docap = 1;
			} else if (capfp != (FILE *)0 && !docap) {
				c = ask("Capture file", c3);
				if (c == 0) {
					fclose(capfp);
					capfp = (FILE *)NULL;
					docap = 0;
				}
				if (c == 1) docap = 1;
			} else if (capfp != (FILE *)0 && docap) {
				c = ask("Capture file", c2);
				if (c == 0) {
					fclose(capfp);
					capfp = (FILE *)NULL;
					docap = 0;
				}
				if (c == 1) docap = 0;
			}
			vt_set(addlf, linewrap, capfp, docap, -1, -1, -1);
			break;
		case 'p': /* Set parameters */
			get_bbp(P_BAUDRATE, P_BITS, P_PARITY);
			m_setparms(portfd, P_BAUDRATE, P_PARITY, P_BITS);
			if (st != NIL_WIN) mode_status();
			quit = 0;
			break;
		case 'k': /* Run kermit */
			kermit();
			break;
		case 'h': /* Hang up */
			do_hang(1);
			break;
		case 'd': /* Dial */
			dialdir();
			break;		
		case 't': /* Terminal emulation */
			c = wselect(13, 3, c8, NIL_FUNLIST,
				"Emulation", stdattr, MFG, MBG);
			if (c > 0) init_emul(c);
			break;
		case 'w': /* Line wrap on-off */	
			if (linewrap < 0) linewrap = 0;
			linewrap = !linewrap;
			vt_set(addlf, linewrap, capfp, docap, -1, -1, -1);
			s = linewrap ? "Linewrap ON" : "Linewrap OFF";
			werror(s);
			break;
		case 'o': /* Configure Minicom */
			(void) config(0);
			break;
		case 's': /* Upload */
			updown('U');
			break;
		case 'r': /* Download */
			updown('D');
			break;	
		case 'j': /* Jump to a shell */
			shjump();
			break;
		case 'g': /* Run script */	
			runscript(1, "", "", "");
			break;
		case 'i': /* Re-init, re-open portfd. */
			cursormode = (cursormode == NORMAL) ? APPL : NORMAL;
			keyboard(cursormode == NORMAL ? KCURST : KCURAPP, 0);
			if (st) curs_status();
			break;
		default:
			break;
	}
  } while(!quit);

  /* Reset parameters */
  if (quit != NORESET)
	m_restorestate(portfd);
  else
	m_hupcl(portfd, 0);
  if (capfp != (FILE *)0) fclose(capfp);
  wclose(us, 0);
  wclose(st, 0);
  wclose(stdwin, 1);
  keyboard(KUNINSTALL, 0);
  if (lockfile[0]) unlink(lockfile);
  close(portfd);
  chown(P_PORT, portuid, portgid);
  if (quit != NORESET && P_CALLIN[0])
	(void) fastsystem(P_CALLIN, CNULL, CNULL, CNULL);
  return(0);
}
