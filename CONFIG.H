/*
 * This file is part of the Minicom Communications Program,
 * written by Miquel van Smoorenburg 1991/1992/1993.
 *
 * config.h  -  default configuration.
 */

/*
 * Definitions below are not hard-coded - you can change them from
 * the setup menu in minicom, or you can start minicom with the
 * "-s" flag.
 * Recommended setting for some systems are commented. Uncomment
 * and adjust them to your system.
 */

/* Operating system INdependant parameters. (Usually the same everywhere) */
#define KERMIT "/usr/bin/kermit -l %l -b %b"	/* How to call kermit */
#define UUCPLOCK	"/usr/spool/uucp"	/* Lock file directory */
#define LOGFILE		"minicom.log"		/* Not defined = not used */

/* Operating system dependant parameters, per OS. A few samples are given. */
#if defined(linux) || defined(__linux)
#  define DFL_PORT "/dev/cua1"		/* Which tty to use */
#  define DEF_BAUD "19200"		/* Default baud rate */
#  define CALLOUT  ""			/* Gets run to get into dial out mode */
#  define CALLIN   ""			/* Gets run to get into dial in mode */
#endif

#ifdef _MINIX
#  define DFL_PORT "/dev/tty1"
#  define DEF_BAUD "2400"
#  define CALLOUT  ""
#  define CALLIN   ""
#endif

#if defined (_COHERENT)
#  define DFL_PORT "/dev/com1l"
#  define DEF_BAUD "2400"
#  define CALLOUT  "/etc/disable com1r"
#  define CALLIN   "/etc/enable com1r"
#endif

#ifdef _HPUX_SOURCE
#  define DFL_PORT "/dev/cua2p0"
#  define DEF_BAUD "19200"
#  define CALLOUT  ""
#  define CALLIN   ""
#endif

/* Some reasonable defaults if not defined */
#ifndef DFL_PORT
#  define DFL_PORT "/dev/tty8"
#  define DEF_BAUD "2400"
#  define CALLIN   ""
#  define CALLOUT  ""
#endif

/*
 * The next definitions are permanent ones - you can't edit the
 * configuration from within minicom to change them
 * (unless you use a binary editor, like a real hacker :-)
 */

/* Menu Colors (for all possible colors - look in window.h) */
#define MFG	YELLOW	/* ForeGround */
#define MBG	BLUE	/* BackGround */

/* Terminal window colors */
#define SFG	BLUE
#define SBG	CYAN

/* The next automatically defines "KEY_KLUDGE" for a Minix system;
 * it improves the algorithm for decoding escape sequences but
 * is very Minix specific (read: DIRTY)
 */
#if defined (_MINIX) || defined(linux) /*Experimental */
#  define KEY_KLUDGE 1
#endif

/*
 * Only and only define this if you have a slow machine and find
 * the output of minicom unaccepably jerky.
 */
#ifdef _MINIX
#  define SMOOTH /* */
#endif

/*
 * Minix and Coherent V3 probably don't have enough memory for a
 * scroll back buffer.
 */
#if defined(_MINIX) || defined(_COH3)
#  define HISTORY 0
#else
#  define HISTORY 1
#endif

#if defined (_SYSV) || defined (_BSD43)
#  if !defined(_SVR2)
#    define HAS_FCHOWN
#    ifdef __linux__
       extern int fchown(); /* Not Posix. */
#    endif
#  endif
#endif
