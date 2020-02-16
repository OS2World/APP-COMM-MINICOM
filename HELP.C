/*
 * This file is part of the Minicom Communications Program,
 * written by Miquel van Smoorenburg 1991/1992/1993.
 */
#include <stdio.h>
#include <sys/types.h>
#include <setjmp.h>
#include "window.h"
#include "minicom.h"
#include "configsym.h"

/* Draw a help screen and return the keypress code. */
int help()
{
  WIN *w;
  int c;
  int i;

#if HISTORY
  i = 1;
#else
  i = 0;
#endif
  w = wopen(6, 3, 72, 19 + i, BDOUBLE, stdattr, MFG, MBG, 0, 0, 1);
  
  wlocate(w, 21, 0);
  wputs(w, "Minicom Command Summary");
  wlocate(w, 10, 2);

  wprintf(w, "Commands can be called by %s<key>", esc_key());

  wlocate(w, 15, 4);
  wputs(w, "Main Functions");
  wlocate(w, 47, 4);
  wputs(w, "Other Functions");
  wlocate(w, 0, 6);
  wputs(w, " Dialing directory..D  run script (Go)....G \263 Clear Screen.......C\n");
  wputs(w, " Send files.........S  Receive files......R \263 cOnfigure Minicom..O\n");
  wputs(w, " comm Parameters....P  Add linefeed.......A \263 Jump to a shell....J\n");
  wputs(w, " Capture on/off.....L  Hangup.............H \263 eXit and reset.....X\n");
  wputs(w, " send break.........F  initialize Modem...M \263 Quit with no reset.Q\n");
  wputs(w, " Terminal emulation.T  run Kermit.........K \263 Cursor key mode....I\n");
  wputs(w, " lineWrap on/off....W");
#ifdef _SELECT
  wputs(w, "  local Echo on/off..E \263 Help screen........Z");
#else
  wputs(w, "                       \263 Help screen........Z");
#endif
#if HISTORY
  wlocate(w, 44, 13);
  wputs(w, "\263 scroll Back........B");
#endif

  wlocate(w, 13, 16 + i);
  wputs(w, "Written by Miquel van Smoorenburg 1992/1993");
  wlocate(w, 6, 14 + i);
  wputs(w, "Select function or press Enter for none.");
  
  wredraw(w, 1);

  c = getch();
  wclose(w, 1);
  return(c);
}
