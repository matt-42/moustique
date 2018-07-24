/**
 * @file   moustique.hh
 * @author Matthieu Garrigues <matthieu.garrigues@gmail.com> <matthieu.garrigues@gmail.com>
 * @date   Sat Mar 31 23:52:51 2018
 * 
 * @brief  moustique wrapper.
 * 
 * 
 */
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>

#include <thread>
#include <vector>
#include <boost/context/all.hpp>

/** 
 * Open a socket on port \port and call \conn_handler(int client_fd, auto read, auto write) 
 * to process each incomming connection. This handle takes 3 argments:
 *               - int client_fd: the file descriptor of the socket.
 *               - int read(buf, max_size_to_read):  
 *                       The callback that conn_handler can use to read on the socket.
 *                       If data is available, copy it into \buf, otherwise suspend the handler until
 *                       there is something to read.
 *                       Returns the number of char actually read, returns 0 if the connection has been lost.
 *               - bool write(buf, buf_size): return true on success, false on error.
 *                       The callback that conn_handler can use to write on the socket.
 *                       If the socket is ready to write, write \buf, otherwise suspend the handler until
 *                       it is ready.
 *                       Returns true on sucess, false if connection is lost.
 *
 * @param port  The server port.
 * @param socktype The socket type, SOCK_STREAM for TCP, SOCK_DGRAM for UDP.
 * @param nthreads Number of threads.
 * @param conn_handler The connection handler
 * @return false on error, true on success.
 */
template <typename H>
int moustique_listen(int port,
                     int socktype,
                     int nthreads,
                     H conn_handler);

// Same as above but take an already opened socket \listen_fd.
template <typename H>
int moustique_listen_fd(int listen_fd,
                        int nthreads,
                        H conn_handler);

namespace moustique_impl
{
  static int create_and_bind(int port, int socktype)
  {
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int s, sfd;

    char port_str[20];
    snprintf(port_str, sizeof(port_str), "%d", port);
    memset (&hints, 0, sizeof (struct addrinfo));
    hints.ai_family = AF_UNSPEC;     /* Return IPv4 and IPv6 choices */
    hints.ai_socktype = socktype; /* We want a TCP socket */
    hints.ai_flags = AI_PASSIVE;     /* All interfaces */

    s = getaddrinfo (NULL, port_str, &hints, &result);
    if (s != 0)
    {
      fprintf (stderr, "getaddrinfo: %s\n", gai_strerror (s));
      return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
      sfd = socket (rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      if (sfd == -1)
        continue;

      int enable = 1;
      if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
      {
        close(sfd);
        continue;
      }

      s = bind (sfd, rp->ai_addr, rp->ai_addrlen);
      if (s == 0)
      {
        /* We managed to bind successfully! */
        break;
      }

      close (sfd);
    }

    freeaddrinfo (result);
    
    if (rp == NULL)
    {
      fprintf (stderr, "Could not bind\n");
      return -1;
    }

    return sfd;
  }

}

#define MOUSTIQUE_CHECK_CALL(CALL)                                      \
  {                                                                     \
    int ret = CALL;                                                     \
    if (-1 == ret)                                                      \
    {                                                                   \
      fprintf(stderr, "Error at %s:%i  error is: %s", __PRETTY_FUNCTION__, __LINE__, strerror(ret)); \
      return false;                                                     \
    }                                                                   \
  }

template <typename H>
int moustique_listen(int port,
                     int socktype,
                     int nthreads,
                     H conn_handler)
{
  return moustique_listen_fd(moustique_impl::create_and_bind(port, socktype), nthreads, conn_handler);
}

template <typename H>
int moustique_listen_fd(int listen_fd,
                        int nthreads,
                        H conn_handler)
{
  namespace ctx = boost::context;

  if (listen_fd < 0) return 0;
  int flags = fcntl (listen_fd, F_GETFL, 0);
  MOUSTIQUE_CHECK_CALL(fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK));
  MOUSTIQUE_CHECK_CALL(::listen(listen_fd, SOMAXCONN));

  auto event_loop_fn = [listen_fd, conn_handler] {

    int epoll_fd = epoll_create1(0);

    auto epoll_ctl = [epoll_fd] (int fd, uint32_t flags) -> bool
    {
      epoll_event event;
      event.data.fd = fd;
      event.events = flags;
      MOUSTIQUE_CHECK_CALL(::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event));
      return true;
    };
    
    epoll_ctl(listen_fd, EPOLLIN | EPOLLET);

    const int MAXEVENTS = 64;
    std::vector<ctx::continuation> fibers;

    // Event loop.
    epoll_event events[MAXEVENTS];
    while (true)
    {
      int n_events = epoll_wait (epoll_fd, events, MAXEVENTS, -1);
      for (int i = 0; i < n_events; i++)
      {
        if ((events[i].events & EPOLLERR) ||
            (events[i].events & EPOLLHUP))
        {
          fibers[events[i].data.fd] = fibers[events[i].data.fd].resume();
          continue;
        }
        else if (listen_fd == events[i].data.fd) // New connection.
        {
          while(true)
          {
            struct sockaddr in_addr;
            socklen_t in_len;
            int infd;
            char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

            in_len = sizeof in_addr;
            infd = accept (listen_fd, &in_addr, &in_len);
            if (infd == -1)
              break;
            getnameinfo (&in_addr, in_len,
                         hbuf, sizeof(hbuf),
                         sbuf, sizeof(sbuf),
                         NI_NUMERICHOST | NI_NUMERICSERV);

            MOUSTIQUE_CHECK_CALL(fcntl(infd, F_SETFL, fcntl(infd, F_GETFL, 0) | O_NONBLOCK));

            epoll_ctl(infd, EPOLLIN | EPOLLOUT | EPOLLET);

            if (int(fibers.size()) < infd + 1)
              fibers.resize(infd + 10);

            fibers[infd] = ctx::callcc([fd=infd, &conn_handler]
                                       (ctx::continuation&& sink) {
                                         auto read = [fd, &sink] (char* buf, int max_size) {
                                           ssize_t count = ::read(fd, buf, max_size);
                                           while (count <= 0)
                                           {
                                             if ((count < 0 and errno != EAGAIN) or count == 0)
                                               return ssize_t(0);
                                             sink = sink.resume();
                                             count = ::read(fd, buf, max_size);
                                           }
                                           return count;
                                         };

                                         auto write = [fd, &sink] (const char* buf, int size) {
                                           const char* end = buf + size;
                                           ssize_t count = ::write(fd, buf, end - buf);
                                           if (count > 0) buf += count;
                                           while (buf != end)
                                           {
                                             if ((count < 0 and errno != EAGAIN) or count == 0)
                                               return false;
                                             sink = sink.resume();
                                             count = ::write(fd, buf, end - buf);
                                             if (count > 0) buf += count;
                                           }
                                           return true;
                                         };
              
                                         conn_handler(fd, read, write);
                                         close(fd);
                                         return std::move(sink);
                                       });
          
          }
          
        }
        else // Data available on existing sockets. Wake up the fiber associated with events[i].data.fd.
          fibers[events[i].data.fd] = fibers[events[i].data.fd].resume();
      }
    }
    
  };

  std::vector<std::thread> ths;
  for (int i = 0; i < nthreads; i++)
    ths.push_back(std::thread([&] { event_loop_fn(); }));

  for (auto& t : ths)
    t.join();

  close(listen_fd);

  return true;
}
