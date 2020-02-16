/*
 * vt100.h	Header file for the vt100 emulator.
 *		Miquel van Smoorenburg, <miquels@drinkel.nl.mugnet.org>
 */

/* Keypad and cursor key modes. */
#define NORMAL	1
#define APPL	2

#define VT100	1
#define MINIX	2
#define ANSI	3

/* Prototypes from vt100.c */
_PROTO( void vt_install, (void(*)(), void (*)(), WIN *));
_PROTO( void vt_init, (int, int, int, int, int));
_PROTO( void vt_set, (int, int, FILE *, int, int, int, int));
_PROTO( void vt_out, (int ));
_PROTO( void vt_send, (int ch));

