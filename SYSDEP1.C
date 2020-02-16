/*
 * This file is part of the Minicom Communications Program,
 * written by Miquel van Smoorenburg 1991/1992/1993.
 *
 * sysdep1.c 	  - system dependant routines.
 *
 * m_dtrtoggle	  - dropt dtr and raise it again
 * m_break	  - send BREAK signal
 * m_getdcd	  - get modem dcd status
 * m_setdcd	  - set modem dcd status
 * m_savestate	  - save modem state
 * m_restorestate - restore saved modem state
 * m_nohang	  - tell driver not to hang up at DTR drop
 * m_hupcl	  - set hangup on close on/off (for Quit without reset)
 * m_setparms	  - set baudrate, parity and number of bits.
 * m_wait	  - wait for child to finish. System dependant too.
 *
 *  If it's possible, Posix termios are preferred.
 */
#include <sys/types.h>
#if defined (_POSIX_SOURCE) || defined(_BSD43)
#  include <stdlib.h>
#  include <unistd.h>
#endif
#ifdef _NO_TERMIOS
#  undef _POSIX_SOURCE
#endif
#ifndef _COH3
#  include <sys/wait.h>
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
#ifdef _HPUX_SOURCE
#  include <sys/modem.h>
#endif
#if defined(_BSD43) || defined (_SYSV)
#  include <sys/ioctl.h>
#endif
#include <stdio.h>
#include <setjmp.h>
#include "window.h"
#include "minicom.h"

/* Be sure we know WEXITSTATUS and WTERMSIG */
#if !defined(_BSD43)
#  ifndef WEXITSTATUS
#    define WEXITSTATUS(s) (((s) >> 8) & 0377)
#  endif
#  ifndef WTERMSIG
#    define WTERMSIG(s) ((s) & 0177)
#  endif
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

/* Different names for the same beast. */
#ifndef TIOCMODG			/* BSD 4.3 */
#  ifdef TIOCMGET
#    define TIOCMODG TIOCMGET		/* Posix */
#  else
#    ifdef MCGETA
#      define TIOCMODG MCGETA		/* HP/UX */
#    endif
#  endif
#endif

#ifndef TIOCMODS
#  ifdef TIOCMSET
#    define TIOCMODS TIOCMSET
#  else
#    ifdef MCSETA
#      define TIOCMODS MCSETA
#    endif
#  endif
#endif

#ifndef TIOCM_CAR			/* BSD + Posix */
#  ifdef MDCD
#    define TIOCM_CAR MDCD		/* HP/UX */
#  endif
#endif

/* Define some thing that might not be there */
#ifndef TANDEM
#  define TANDEM 0
#endif
#ifndef BITS8
#  define BITS8 0
#endif
#ifndef PASS8
#  ifdef LLITOUT
#  define PASS8 LLITOUT
#  else
#  define PASS8 0
#  endif
#endif
#ifndef CRTSCTS
#  define CRTSCTS 0
#endif

/* If this is SysV without Posix, emulate Posix. */
#if defined(_SYSV) && !defined(_POSIX_SOURCE)
#  define termio termios
#  define _POSIX_SOURCE
#  ifndef TCSANOW
#    define TCSANOW 0
#  endif
#  define tcgetattr(fd, tty)        ioctl(fd, TCGETA, tty)
#  define tcsetattr(fd, flags, tty) ioctl(fd, TCSETA, tty)
#  define tcsendbreak(fd, len)      ioctl(fd, TCSBRK, 0)
#  define speed_t int
#  define cfsetispeed(xtty, xspd) \
		((xtty)->c_cflag = ((xtty)->c_cflag & ~CBAUD) | (xspd))
#  define cfsetospeed(tty, spd)
#endif

/*
 * Drop DTR line and raise it again.
 */
void m_dtrtoggle(fd) 
int fd;
{
#if defined (_POSIX_SOURCE) && !defined(_HPUX_SOURCE)
  struct termios tty, old;

  tcgetattr(fd, &tty);
  tcgetattr(fd, &old);
  cfsetospeed(&tty, B0);
  cfsetispeed(&tty, B0);
  tcsetattr(fd, TCSANOW, &tty);
  sleep(1);
  tcsetattr(fd, TCSANOW, &old);
#else
#  ifdef _V7
#    ifndef TIOCCDTR
  /* Just drop speed to 0 and back to normal again */
  struct sgttyb sg, ng;
  
  ioctl(fd, TIOCGETP, &sg);
  ioctl(fd, TIOCGETP, &ng);
  
  ng.sg_ispeed = ng.sg_ospeed = 0;
  ioctl(fd, TIOCSETP, &ng);
  sleep(1);
  ioctl(fd, TIOCSETP, &sg);
#    else
  /* Use the ioctls meant for this type of thing. */
  ioctl(fd, TIOCCDTR, 0);
  sleep(1);
  ioctl(fd, TIOCSDTR, 0);
#    endif
#  endif
#  ifdef _HPUX_SOURCE
  unsigned long mflag = 0L;

  ioctl(fd, MCSETAF, &mflag);
  ioctl(fd, MCGETA, &mflag);
  mflag = MRTS | MDTR;
  sleep(1);
  ioctl(fd, MCSETAF, &mflag);
#  endif
#endif
}

/*
 * Send a break
 */
void m_break(fd)
int fd;
{ 
#ifdef _POSIX_SOURCE
#  ifdef linux
  /* Linux kernels < .99pl8 don't support tcsendbreak() yet.. */
  ioctl(fd, TCSBRK, 0);
#  else
  tcsendbreak(fd, 0);
#  endif
#else
#  ifdef _V7
#    ifndef TIOCSBRK
  struct sgttyb sg, ng;

  ioctl(fd, TIOCGETP, &sg);
  ioctl(fd, TIOCGETP, &ng);
  ng.sg_ispeed = ng.sg_ospeed = B110;
  ng.sg_flags = BITS8 | RAW;
  ioctl(fd, TIOCSETP, &ng);
  write(fd, "\0\0\0\0\0\0\0\0\0\0", 10);
  ioctl(fd, TIOCSETP, &sg);
#    else
  ioctl(fd, TIOCSBRK, 0);
  sleep(1);
  ioctl(fd, TIOCCBRK, 0);
#    endif
#  endif
#endif
}

/*
 * Get the dcd status
 */
int m_getdcd(fd)
int fd;
{
#ifdef _MINIX
  struct sgttyb sg;
  
  ioctl(fd, TIOCGETP, &sg);
  return(sg.sg_flags & DCD ? 1 : 0);
#else
#  ifdef TIOCMODG
  int mcs;
   
  ioctl(fd, TIOCMODG, &mcs);
  return(mcs & TIOCM_CAR ? 1 : 0);
#  else
  return(0); /* Impossible!! */
#  endif
#endif
}

/*
 * Set the DCD status
 */
/*ARGSUSED*/
void m_setdcd(fd, what)
int fd, what;
{
#ifdef _MINIX
  /* Just a kludge for my Minix rs 232 driver */
  struct sgttyb sg;
  
  ioctl(fd, TIOCGETP, &sg);
  if (what)
  	sg.sg_flags |= DCD;
  else
  	sg.sg_flags &= ~DCD;
  ioctl(fd, TIOCSETP, &sg);
#endif
}

/* Variables to save states in */
#ifdef _POSIX_SOURCE
static struct termios savetty;
static int m_word;
#else
#  if defined (_BSD43) || defined (_V7)
static struct sgttyb sg;
static struct tchars tch;
static int lsw;
static int m_word;
#  endif
#endif

/*
 * Save the state of a port
 */
void m_savestate(fd)
int fd;
{
#ifdef _POSIX_SOURCE
  tcgetattr(fd, &savetty);
#else
#  if defined(_BSD43) || defined(_V7)
  ioctl(fd, TIOCGETP, &sg);
  ioctl(fd, TIOCGETC, &tch);
#  endif
#  ifdef _BSD43
  ioctl(fd, TIOCLGET, &lsw);
#  endif
#endif
#ifdef TIOCMODG
  ioctl(fd, TIOCMODG, &m_word);
#endif
}

/*
 * Restore the state of a port
 */
void m_restorestate(fd)
int fd;
{
#ifdef _POSIX_SOURCE
  tcsetattr(fd, TCSANOW, &savetty);
#else
#  if defined(_BSD43) || defined(_V7)
  ioctl(fd, TIOCSETP, &sg);
  ioctl(fd, TIOCSETC, &tch);
#  endif
#  ifdef _BSD43  
  ioctl(fd, TIOCLSET, &lsw);
#  endif
#endif
#ifdef TIOCMODS
  ioctl(fd, TIOCMODS, &m_word);
#endif
}

/*
 * Set the line status so that it will not kill our process
 * if the line hangs up.
 */
/*ARGSUSED*/ 
void m_nohang(fd)
int fd;
{
#ifdef _POSIX_SOURCE
  struct termios sgg;
  
  tcgetattr(fd, &sgg);
  sgg.c_cflag |= CLOCAL;
  tcsetattr(fd, TCSANOW, &sgg);
#else
#  if defined (_BSD43) && defined(LNOHANG)
  int lsw;
  
  ioctl(fd, TIOCLGET, &lsw);
  lsw |= LNOHANG;
  ioctl(fd, TIOCLSET, &lsw);
#  endif
#  ifdef _MINIX
  /* So? What about 1.6 ? */
#  endif
#  ifdef _COHERENT
  /* Doesn't know about this either, me thinks. */
#  endif
#endif
}

/*
 * Set hangup on close on/off.
 */
void m_hupcl(fd, on)
int fd;
int on;
{
  /* Eh, I don't know how to do this under BSD (yet..) */
#ifdef _POSIX_SOURCE
  struct termios sgg;
  
  tcgetattr(fd, &sgg);
  if (on)
  	sgg.c_cflag |= HUPCL;
  else
	sgg.c_cflag &= ~HUPCL;
  tcsetattr(fd, TCSANOW, &sgg);
#endif
}

/*
 * Flush the buffers
 */
void m_flush(fd)
int fd;
{
/* Should I Posixify this, or not? */
#ifdef TCFLSH
  ioctl(fd, TCFLSH, 2);
#endif
#ifdef TIOCFLUSH
#ifdef _COHERENT
  ioctl(fd, TIOCFLUSH, 0);
#else
  ioctl(fd, TIOCFLUSH, (void *)0);
#endif
#endif
}

/*
 * Set baudrate, parity and number of bits.
 */
void m_setparms(fd, baudr, par, bits)
int fd;
char *baudr;
char *par;
char *bits;
{
  int spd = -1;

#ifdef _POSIX_SOURCE
  struct termios tty;

  tcgetattr(fd, &tty);
#else
  struct sgttyb tty;

  ioctl(fd, TIOCGETP, &tty);
#endif

  switch(atoi(baudr)) {
  	case 0:
#ifdef B0
			spd = B0;	break;
#else
			spd = 0;	break;
#endif
  	case 300:	spd = B300;	break;
  	case 600:	spd = B600;	break;
  	case 1200:	spd = B1200;	break;
  	case 2400:	spd = B2400;	break;
  	case 4800:	spd = B4800;	break;
  	case 9600:	spd = B9600;	break;
#ifdef B19200
  	case 19200:	spd = B19200;	break;
#else
#  ifdef EXTA
	case 19200:	spd = EXTA;	break;
#   else
	case 19200:	spd = B9600;	break;
#   endif	
#endif	
#ifdef B38400
  	case 38400:	spd = B38400;	break;
#else
#  ifdef EXTA
	case 38400:	spd = EXTA;	break;
#   else
	case 38400:	spd = B9600;	break;
#   endif
#endif	

  }
  
#if defined (_BSD43) && !defined(_POSIX_SOURCE)
  if (spd != -1) tty.sg_ispeed = tty.sg_ospeed = spd;
  /* Number of bits is ignored */

  tty.sg_flags = RAW | TANDEM;
  if (par[0] == 'E')
	tty.sg_flags |= EVENP;
  else if (par[0] == 'O')
	tty.sg_flags |= ODDP;
  else
  	tty.sg_flags |= PASS8 | ANYP;

  ioctl(fd, TIOCSETP, &tty);

#  ifdef TIOCSDTR
  ioctl(fd, TIOCSDTR, 0);
#  endif
#endif

#if defined (_V7) && !defined(_POSIX_SOURCE)
  if (spd != -1) tty.sg_ispeed = tty.sg_ospeed = spd;
#ifdef _MINIX
  switch(bits[0]) {
  	case '5' : spd = BITS5; break;
  	case '6' : spd = BITS6; break;
  	case '7' : spd = BITS7; break;
  	case '8' :
  	default: spd = BITS8; break;
  }
  tty.sg_flags = RAW | spd;
#else
  tty.sg_flags = RAW;
#endif
  if (par[0] == 'E')
	tty.sg_flags |= EVENP;
  else if (par[0] == 'O')
	tty.sg_flags |= ODDP;

  ioctl(fd, TIOCSETP, &tty);
#endif

#ifdef _POSIX_SOURCE

  cfsetospeed(&tty, (speed_t)spd);
  cfsetispeed(&tty, (speed_t)spd);

  switch (bits[0]) {
  	case '5':
  		tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS5;
  		break;
  	case '6':
  		tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS6;
  		break;
  	case '7':
  		tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS7;
  		break;
  	case '8':
	default:
  		tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
  		break;
  }		
  /* Set into raw, no echo mode */
  tty.c_iflag &= ~(IGNBRK | IGNCR | INLCR | ICRNL | IUCLC | 
  	IXANY | IXON | IXOFF | INPCK | ISTRIP);
  tty.c_iflag |= (BRKINT | IGNPAR);
  tty.c_oflag &= ~OPOST;
  tty.c_lflag = ~(ICANON | ISIG | ECHO | ECHONL | ECHOE | ECHOK);
  tty.c_cflag |= CREAD | CRTSCTS;
  tty.c_cc[VMIN] = 1;
  tty.c_cc[VTIME] = 5;

  tty.c_cflag &= ~(PARENB | PARODD);
  if (par[0] == 'E')
	tty.c_cflag |= PARENB;
  else if (par[0] == 'O')
	tty.c_cflag |= PARODD;

  tcsetattr(fd, TCSANOW, &tty);
#endif
}

/*
 * Wait for child and return pid + status
 */
int m_wait(stt)
int *stt;
{
#if defined (_BSD43) && !defined(_POSIX_SOURCE)
  int pid;
  union wait st1;
  
  pid = wait((void *)&st1);
  *stt = (unsigned)st1.w_retcode + 256 * (unsigned)st1.w_termsig;
  return(pid);
#else
  int pid;
  int st1;
  
  pid = wait(&st1);
  *stt = (unsigned)WEXITSTATUS(st1) + 256 * (unsigned)WTERMSIG(st1);
  return(pid);
#endif
}
