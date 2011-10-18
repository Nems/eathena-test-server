// $Id: core.c,v 1.1.1.1 2004/09/10 17:44:49 MagicalTux Exp $
// original : core.c 2003/02/26 18:03:12 Rev 1.7

#include <stdio.h>
#include <stdlib.h>
#ifndef LCCWIN32
#include <unistd.h>
#endif
#include <signal.h>
#include <sys/wait.h>

#include "core.h"
#include "socket.h"
#include "timer.h"
#include "version.h"
#include "mt_rand.h"

#ifdef MEMWATCH
#include "memwatch.h"
#endif

static void (*term_func) (void) = NULL;

/*======================================
 *	CORE : Set function
 *--------------------------------------
 */
void set_termfunc (void (*termfunc) (void))
{
    term_func = termfunc;
}

/*======================================
 *	CORE : Signal Sub Function
 *--------------------------------------
 */

static void sig_proc (int sn)
{
    int  i;
    switch (sn)
    {
        case SIGINT:
        case SIGTERM:
            if (term_func)
                term_func ();
            for (i = 0; i < fd_max; i++)
            {
                if (!session[i])
                    continue;
                close (i);
            }
            exit (0);
            break;
        case SIGCHLD:
            wait (&i);
            break;
    }
}

int  eathena_interactive_session = 1;   // [fate] interactive session:  print pretty graphics

/*======================================
 *	CORE : Display title
 *--------------------------------------
 */

static void display_title (void)
{
    // for help with the console colors look here:
    // http://www.edoceo.com/liberum/?doc=printf-with-color
    // some code explanation (used here):
    // \033[2J : clear screen and go up/left (0, 0 position)
    // \033[K  : clear line from actual position to end of the line
    // \033[0m : reset color parameter
    // \033[1m : use bold for font
    if (eathena_interactive_session)
    {
        printf ("\033[2J");     // clear screen and go up/left (0, 0 position in text)
        printf ("\033[37;44m          (=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=)\033[K\033[0m\n"); // white writing (37) on blue background (44), \033[K clean until end of file
        printf ("\033[0;44m          (\033[1;33m        (c)2004 eAthena Development Team presents        \033[0;44m)\033[K\033[0m\n");  // yellow writing (33)
        printf ("\033[0;44m          (\033[1m       ______  __    __                                  \033[0;44m)\033[K\033[0m\n"); // 1: bold char, 0: normal char
        printf ("\033[0;44m          (\033[1m      /\\  _  \\/\\ \\__/\\ \\                     v%2d.%02d.%02d   \033[0;44m)\033[K\033[0m\n", ATHENA_MAJOR_VERSION, ATHENA_MINOR_VERSION, ATHENA_REVISION); // 1: bold char, 0: normal char
        printf ("\033[0;44m          (\033[1m    __\\ \\ \\_\\ \\ \\ ,_\\ \\ \\___      __    ___      __      \033[0;44m)\033[K\033[0m\n");    // 1: bold char, 0: normal char
        printf ("\033[0;44m          (\033[1m  /'__`\\ \\  __ \\ \\ \\/\\ \\  _ `\\  /'__`\\/' _ `\\  /'__`\\    \033[0;44m)\033[K\033[0m\n");  // 1: bold char, 0: normal char
        printf ("\033[0;44m          (\033[1m /\\  __/\\ \\ \\/\\ \\ \\ \\_\\ \\ \\ \\ \\/\\  __//\\ \\/\\ \\/\\ \\_\\.\\_  \033[0;44m)\033[K\033[0m\n");   // 1: bold char, 0: normal char
        printf ("\033[0;44m          (\033[1m \\ \\____\\\\ \\_\\ \\_\\ \\__\\\\ \\_\\ \\_\\ \\____\\ \\_\\ \\_\\ \\__/.\\_\\ \033[0;44m)\033[K\033[0m\n"); // 1: bold char, 0: normal char
        printf ("\033[0;44m          (\033[1m  \\/____/ \\/_/\\/_/\\/__/ \\/_/\\/_/\\/____/\\/_/\\/_/\\/__/\\/_/ \033[0;44m)\033[K\033[0m\n");  // 1: bold char, 0: normal char
        printf ("\033[0;44m          (\033[1m   _   _   _   _   _   _   _     _   _   _   _   _   _   \033[0;44m)\033[K\033[0m\n"); // 1: bold char, 0: normal char
        printf ("\033[0;44m          (\033[1m  / \\ / \\ / \\ / \\ / \\ / \\ / \\   / \\ / \\ / \\ / \\ / \\ / \\  \033[0;44m)\033[K\033[0m\n");    // 1: bold char, 0: normal char
        printf ("\033[0;44m          (\033[1m ( e | n | g | l | i | s | h ) ( A | t | h | e | n | a ) \033[0;44m)\033[K\033[0m\n"); // 1: bold char, 0: normal char
        printf ("\033[0;44m          (\033[1m  \\_/ \\_/ \\_/ \\_/ \\_/ \\_/ \\_/   \\_/ \\_/ \\_/ \\_/ \\_/ \\_/  \033[0;44m)\033[K\033[0m\n");    // 1: bold char, 0: normal char
        printf ("\033[37;44m          (=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=)\033[K\033[0m\n\n");   // reset color
    }
}

// Added by Gabuzomeu
//
// This is an implementation of signal() using sigaction() for portability.
// (sigaction() is POSIX; signal() is not.)  Taken from Stevens' _Advanced
// Programming in the UNIX Environment_.
//
#ifndef SIGPIPE
#define SIGPIPE SIGINT
#endif

#ifndef POSIX
#define compat_signal(signo, func) signal(signo, func)
#else
sigfunc *compat_signal (int signo, sigfunc * func)
{
    struct sigaction sact, oact;

    sact.sa_handler = func;
    sigemptyset (&sact.sa_mask);
    sact.sa_flags = 0;
#ifdef SA_INTERRUPT
    sact.sa_flags |= SA_INTERRUPT;  /* SunOS */
#endif

    if (sigaction (signo, &sact, &oact) < 0)
        return (SIG_ERR);

    return (oact.sa_handler);
}
#endif

/*======================================
 *	CORE : MAINROUTINE
 *--------------------------------------
 */

int  runflag = 1;

int main (int argc, char **argv)
{
    int  next;

    mt_seed (time (NULL) ^ getpid () ^ getppid ());

    Net_Init ();
    do_socket ();

    compat_signal (SIGPIPE, SIG_IGN);
    compat_signal (SIGTERM, sig_proc);
    compat_signal (SIGINT, sig_proc);
    compat_signal (SIGCHLD, sig_proc);

    // Signal to create coredumps by system when necessary (crash)
    compat_signal (SIGSEGV, SIG_DFL);
#ifndef LCCWIN32
    compat_signal (SIGBUS, SIG_DFL);
    compat_signal (SIGTRAP, SIG_DFL);
#endif
    compat_signal (SIGILL, SIG_DFL);
    // [fate] detect interactive session
    eathena_interactive_session = isatty (0);
    display_title ();

    do_init (argc, argv);
    while (runflag)
    {
        next = do_timer (gettick_nocache ());
        do_sendrecv (next);
        do_parsepacket ();
    }
    return 0;
}
