// $Id: socket.c,v 1.1.1.1 2004/09/10 17:44:49 MagicalTux Exp $
// original : core.c 2003/02/26 18:03:12 Rev 1.7

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>

#ifdef LCCWIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <fcntl.h>
#include <string.h>

#include "mmo.h"                // [Valaris] thanks to fov
#include "socket.h"
#include "utils.h"

#ifdef MEMWATCH
#include "memwatch.h"
#endif

fd_set readfds;
int  fd_max;
int  currentuse;

int  rfifo_size = 65536;
int  wfifo_size = 65536;

struct socket_data *session[FD_SETSIZE];

static int null_parse (int fd);
static int (*default_func_parse) (int) = null_parse;

/*======================================
 *	CORE : Set function
 *--------------------------------------
 */
void set_defaultparse (int (*defaultparse) (int))
{
    default_func_parse = defaultparse;
}

/*======================================
 *	CORE : Socket Sub Function
 *--------------------------------------
 */

static int recv_to_fifo (int fd)
{
    int  len;

    //printf("recv_to_fifo : %d %d\n",fd,session[fd]->eof);
    if (session[fd]->eof)
        return -1;

#ifdef LCCWIN32
    len =
        recv (fd, session[fd]->rdata + session[fd]->rdata_size,
              RFIFOSPACE (fd), 0);
#else
    len =
        read (fd, session[fd]->rdata + session[fd]->rdata_size,
              RFIFOSPACE (fd));
#endif

//  printf (":::RECEIVE:::\n");
//  dump(session[fd]->rdata, len); printf ("\n");

    //{ int i; printf("recv %d : ",fd); for(i=0;i<len;i++){ printf("%02x ",RFIFOB(fd,session[fd]->rdata_size+i)); } printf("\n");}
    if (len > 0)
    {
        session[fd]->rdata_size += len;
        if (!session[fd]->connected)
            session[fd]->connected = 1;
    }
    else if (len <= 0)
    {
        // value of connection is not necessary the same
//      if (fd == 4)            // Removed [Yor]
//          printf("Char-Server Has Disconnected.\n");
//      else if (fd == 5)       // Removed [Yor]
//          printf("Attempt To Log In Successful.\n");
//      else if (fd == 7)       // Removed [Yor]
//          printf("Char-Server Has Disconnected.\n");
//      else if (fd == 8)       // Removed [Valaris]
//          printf("%s has logged off your server.\n",RFIFOP(fd,6));    // Removed [Valaris]

//      else if (fd != 8)   // [Valaris]
        printf ("set eof : connection #%d\n", fd);
        session[fd]->eof = 1;
    }
    return 0;
}

static int send_from_fifo (int fd)
{
    int  len;

    //printf("send_from_fifo : %d\n",fd);
    if (session[fd]->eof)
        return -1;

#ifdef LCCWIN32
    len = send (fd, session[fd]->wdata, session[fd]->wdata_size, 0);
#else
    len = write (fd, session[fd]->wdata, session[fd]->wdata_size);
#endif

//  printf (":::SEND:::\n");
//  dump(session[fd]->wdata, len); printf ("\n");

    //{ int i; printf("send %d : ",fd);  for(i=0;i<len;i++){ printf("%02x ",session[fd]->wdata[i]); } printf("\n");}
    if (len > 0)
    {
        if (len < session[fd]->wdata_size)
        {
            memmove (session[fd]->wdata, session[fd]->wdata + len,
                     session[fd]->wdata_size - len);
            session[fd]->wdata_size -= len;
        }
        else
        {
            session[fd]->wdata_size = 0;
        }
        if (!session[fd]->connected)
            session[fd]->connected = 1;
    }
    else
    {
        printf ("set eof :%d\n", fd);
        session[fd]->eof = 1;
    }
    return 0;
}

static int null_parse (int fd)
{
    printf ("null_parse : %d\n", fd);
    RFIFOSKIP (fd, RFIFOREST (fd));
    return 0;
}

/*======================================
 *	CORE : Socket Function
 *--------------------------------------
 */

static int connect_client (int listen_fd)
{
    int  fd;
    struct sockaddr_in client_address;
    unsigned int len;
    int  result;
    int  yes = 1;               // reuse fix

    //printf("connect_client : %d\n",listen_fd);

    printf ("used: %d, max FDs: %d, SOFT: %d\n", currentuse, FD_SETSIZE,
            SOFT_LIMIT);

    len = sizeof (client_address);

    fd = accept (listen_fd, (struct sockaddr *) &client_address, &len);
    if (fd_max <= fd)
    {
        fd_max = fd + 1;
    }
    else if (fd == -1)
    {
        perror ("accept");
        return -1;
    }
    if (!free_fds ())
    {                           // gracefully end the connecting if no free FD
        printf ("softlimit reached, disconnecting : %d\n", fd);
        delete_session (fd);
        return -1;
    }

//  setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,NULL,0);
    setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, (char *) &yes, sizeof yes);   // reuse fix
#ifdef SO_REUSEPORT
//  setsockopt(fd,SOL_SOCKET,SO_REUSEPORT,NULL,0);
    setsockopt (fd, SOL_SOCKET, SO_REUSEPORT, (char *) &yes, sizeof yes);   //reuse fix
#endif
//  setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,NULL,0);
    setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, (char *) &yes, sizeof yes);   // reuse fix

    FD_SET (fd, &readfds);

#ifdef LCCWIN32
    {
        unsigned long val = 1;
        ioctlsocket (fd, FIONBIO, &val);
    }
#else
    result = fcntl (fd, F_SETFL, O_NONBLOCK);
#endif

    CREATE (session[fd], struct socket_data, 1);
    CREATE (session[fd]->rdata, unsigned char, rfifo_size);
    CREATE (session[fd]->wdata, unsigned char, wfifo_size);

    session[fd]->max_rdata = rfifo_size;
    session[fd]->max_wdata = wfifo_size;
    session[fd]->func_recv = recv_to_fifo;
    session[fd]->func_send = send_from_fifo;
    session[fd]->func_parse = default_func_parse;
    session[fd]->client_addr = client_address;
    session[fd]->created = time (NULL);
    session[fd]->connected = 0;

    currentuse++;

    //printf("new_session : %d %d\n",fd,session[fd]->eof);
    return fd;
}

int make_listen_port (int port)
{
    struct sockaddr_in server_address;
    int  fd;
    int  result;
    int  yes = 1;               // reuse fix

    fd = socket (AF_INET, SOCK_STREAM, 0);
    if (fd_max <= fd)
        fd_max = fd + 1;
    else if (fd == -1)
    {
        perror ("connect");
        return -1;
    }

#ifdef LCCWIN32
    {
        unsigned long val = 1;
        ioctlsocket (fd, FIONBIO, &val);
    }
#else
    result = fcntl (fd, F_SETFL, O_NONBLOCK);
#endif

//  setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,NULL,0);
    setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, (char *) &yes, sizeof yes);   // reuse fix
#ifdef SO_REUSEPORT
//  setsockopt(fd,SOL_SOCKET,SO_REUSEPORT,NULL,0);
    setsockopt (fd, SOL_SOCKET, SO_REUSEPORT, (char *) &yes, sizeof yes);   //reuse fix
#endif
//  setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,NULL,0);
    setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, (char *) &yes, sizeof yes);   // reuse fix

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl (INADDR_ANY);
    server_address.sin_port = htons (port);

    result =
        bind (fd, (struct sockaddr *) &server_address,
              sizeof (server_address));
    if (result == -1)
    {
        perror ("bind");
        exit (1);
    }
    result = listen (fd, 5);
    if (result == -1)
    {                           /* error */
        perror ("listen");
        exit (1);
    }

    FD_SET (fd, &readfds);

    CREATE (session[fd], struct socket_data, 1);

    if (session[fd] == NULL)
    {
        printf ("out of memory : make_listen_port\n");
        exit (1);
    }
    memset (session[fd], 0, sizeof (*session[fd]));
    session[fd]->func_recv = connect_client;
    session[fd]->created = time (NULL);
    session[fd]->connected = 1;

    currentuse++;
    return fd;
}

int make_connection (long ip, int port)
{
    struct sockaddr_in server_address;
    int  fd;
    int  result;
    int  yes = 1;               // reuse fix

    fd = socket (AF_INET, SOCK_STREAM, 0);
    if (fd_max <= fd)
        fd_max = fd + 1;
    else if (fd == -1)
    {
        perror ("socket");
        return -1;
    }

//  setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,NULL,0);
    setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, (char *) &yes, sizeof yes);   // reuse fix
#ifdef SO_REUSEPORT
//  setsockopt(fd,SOL_SOCKET,SO_REUSEPORT,NULL,0);
    setsockopt (fd, SOL_SOCKET, SO_REUSEPORT, (char *) &yes, sizeof yes);   //reuse fix
#endif
//  setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,NULL,0);
    setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, (char *) &yes, sizeof yes);   // reuse fix

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = ip;
    server_address.sin_port = htons (port);

#ifdef LCCWIN32
    {
        unsigned long val = 1;
        ioctlsocket (fd, FIONBIO, &val);
    }
#else
    result = fcntl (fd, F_SETFL, O_NONBLOCK);
#endif

    result =
        connect (fd, (struct sockaddr *) (&server_address),
                 sizeof (struct sockaddr_in));

    FD_SET (fd, &readfds);

    CREATE (session[fd], struct socket_data, 1);
    CREATE (session[fd]->rdata, unsigned char, rfifo_size);
    CREATE (session[fd]->wdata, unsigned char, wfifo_size);

    session[fd]->max_rdata = rfifo_size;
    session[fd]->max_wdata = wfifo_size;
    session[fd]->func_recv = recv_to_fifo;
    session[fd]->func_send = send_from_fifo;
    session[fd]->func_parse = default_func_parse;
    session[fd]->created = time (NULL);
    session[fd]->connected = 1;

    currentuse++;
    return fd;
}

int delete_session (int fd)
{
    if (fd < 0 || fd >= FD_SETSIZE)
        return -1;
    FD_CLR (fd, &readfds);
    if (session[fd])
    {
        if (session[fd]->rdata)
            free (session[fd]->rdata);
        if (session[fd]->wdata)
            free (session[fd]->wdata);
        if (session[fd]->session_data)
            free (session[fd]->session_data);
        free (session[fd]);
    }
    session[fd] = NULL;
    shutdown (fd, SHUT_RDWR);
    close (fd);
    currentuse--;
    if (currentuse < 0)
    {
        printf ("delete_session: current sessions negative!\n");
        currentuse = 0;
    }
    //printf("delete_session:%d\n",fd);
    return 0;
}

int realloc_fifo (int fd, int rfifo_size, int wfifo_size)
{
    struct socket_data *s = session[fd];
    if (s->max_rdata != rfifo_size && s->rdata_size < rfifo_size)
    {
        RECREATE (s->rdata, unsigned char, rfifo_size);
        s->max_rdata = rfifo_size;
    }
    if (s->max_wdata != wfifo_size && s->wdata_size < wfifo_size)
    {
        RECREATE (s->wdata, unsigned char, wfifo_size);
        s->max_wdata = wfifo_size;
    }
    return 0;
}

int WFIFOSET (int fd, int len)
{
    struct socket_data *s = session[fd];
    if (s->wdata_size + len + 16384 > s->max_wdata)
    {
        realloc_fifo (fd, s->max_rdata, s->max_wdata << 1);
        printf ("socket: %d wdata expanded to %d bytes.\n", fd, s->max_wdata);
    }
    s->wdata_size = (s->wdata_size + (len) + 2048 < s->max_wdata) ?
        s->wdata_size + len : (printf ("socket: %d wdata lost !!\n", fd),
                               s->wdata_size);
    return 0;
}

int do_sendrecv (int next)
{
    fd_set rfd, wfd;
    struct timeval timeout;
    int  ret, i;

    rfd = readfds;
    FD_ZERO (&wfd);
    for (i = 0; i < fd_max; i++)
    {
        if (!session[i] && FD_ISSET (i, &readfds))
        {
            printf ("force clr fds %d\n", i);
            FD_CLR (i, &readfds);
            continue;
        }
        if (!session[i])
            continue;
        if (session[i]->wdata_size)
            FD_SET (i, &wfd);
    }
    timeout.tv_sec = next / 1000;
    timeout.tv_usec = next % 1000 * 1000;
    ret = select (fd_max, &rfd, &wfd, NULL, &timeout);
    if (ret <= 0)
        return 0;
    for (i = 0; i < fd_max; i++)
    {
        if (!session[i])
            continue;
        if (FD_ISSET (i, &wfd))
        {
            //printf("write:%d\n",i);
            if (session[i]->func_send)
                //send_from_fifo(i);
                session[i]->func_send (i);
        }
        if (FD_ISSET (i, &rfd))
        {
            //printf("read:%d\n",i);
            if (session[i]->func_recv)
                //recv_to_fifo(i);
                session[i]->func_recv (i);
        }
    }
    return 0;
}

int do_parsepacket (void)
{
    int  i;
    for (i = 0; i < fd_max; i++)
    {
        if (!session[i])
            continue;
        if (!session[i]->connected
            && time (NULL) - session[i]->created > CONNECT_TIMEOUT)
        {
            printf ("Session #%d timed out\n", i);
            session[i]->eof = 1;
        }
        if (session[i]->rdata_size == 0 && session[i]->eof == 0)
            continue;
        if (session[i]->func_parse)
        {
            session[i]->func_parse (i);
            if (!session[i])
                continue;
        }
        RFIFOFLUSH (i);
    }
    return 0;
}

void do_socket (void)
{
    FD_ZERO (&readfds);
    currentuse = 2;
}

int RFIFOSKIP (int fd, int len)
{
    struct socket_data *s = session[fd];

    if (s->rdata_size - s->rdata_pos - len < 0)
    {
        fprintf (stderr, "too many skip\n");
        exit (1);
    }

    s->rdata_pos = s->rdata_pos + len;

    return 0;
}

int Net_Init (void)
{
#ifdef LCCWIN32
    /* Start up the windows networking */
    WORD version_wanted = MAKEWORD (1, 1);
    WSADATA wsaData;

    if (WSAStartup (version_wanted, &wsaData) != 0)
    {
        printf ("SYSERR: WinSock not available!\n");
        exit (1);
    }
#endif

    return (0);
}

int fclose_ (FILE * fp)
{
    int  res = fclose (fp);
    if (res == 0)
        currentuse--;
//  printf("file closed: used: %d\n",currentuse);
    return res;
}

FILE *fopen_ (const char *path, const char *mode)
{
    FILE *f = fopen (path, mode);
    if (f != NULL)
        currentuse++;
//  printf("file opened: used: %d\n",currentuse);
    return f;
}

int free_fds ()
{
    return (currentuse + 1 < SOFT_LIMIT) ? 1 : 0;
}
