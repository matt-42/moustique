/**
 * @file   epollpp.hh
 * @author Matthieu Garrigues <matthieu.garrigues@gmail.com> <matthieu.garrigues@gmail.com>
 * @date   Sat Mar 31 23:52:51 2018
 * 
 * @brief  epollpp wrapper.
 * 
 * 
 */
#pragma once

extern "C" {
  
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

}

#include <vector>
#include <boost/context/all.hpp>


/** 
 * Open a TCP socket on port \port and call:
 *   - new_connection_handler(int client_fd) whenever a new client is connected on the server.
 *   - closed_connection_handler(int client_fd) whenever a client connection is lost.
 *   - data_handler(int client_fd, auto read, auto write)
 *
 * @return -1 on error, 0 on success.
 */
template <typename G, typename H>
int epollpp_listen(const char* port,
                   G closed_connection_handler,
                   H data_handler);

// Same as above but take an already opened socket \listen_fd.
template <typename G, typename H>
int epollpp_listen(int listen_fd,
                   G closed_connection_handler, // void(int fd)
                   H data_handler); // void(int fd, const char* data, int data_size)

namespace epollpp_impl
{
  static int create_and_bind(const char *port)
  {
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int s, sfd;

    memset (&hints, 0, sizeof (struct addrinfo));
    hints.ai_family = AF_UNSPEC;     /* Return IPv4 and IPv6 choices */
    hints.ai_socktype = SOCK_STREAM; /* We want a TCP socket */
    hints.ai_flags = AI_PASSIVE;     /* All interfaces */

    s = getaddrinfo (NULL, port, &hints, &result);
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

      s = bind (sfd, rp->ai_addr, rp->ai_addrlen);
      if (s == 0)
      {
        /* We managed to bind successfully! */
        break;
      }

      close (sfd);
    }

    if (rp == NULL)
    {
      fprintf (stderr, "Could not bind\n");
      return -1;
    }

    freeaddrinfo (result);

    return sfd;
  }

}

template <typename G, typename H>
int epollpp_listen(const char* service,
                   G closed_connection_handler,
                   H data_handler)
{
  return epollpp_listen(epollpp_impl::create_and_bind(service),
                        closed_connection_handler, data_handler);
}

template <typename G, typename H>
int epollpp_listen(int listen_fd,
                   G closed_connection_handler,
                   H data_handler)
{
  namespace ctx = boost::context;

  if (listen_fd < 0) return -1;
  const int max_connections = 1000;
  int ret;
  int flags = fcntl (listen_fd, F_GETFL, 0);
  if (-1 == (ret = fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK)))
  {
    fprintf(stderr, "fcntl failed at %s:%s  error is: ", __PRETTY_FUNCTION__, __LINE__, strerror(ret));
    return -1;
  }
  if (-1 == (ret = ::listen(listen_fd, SOMAXCONN)))
  {
    fprintf(stderr, "listen failed at %s:%s  error is: ", __PRETTY_FUNCTION__, __LINE__, strerror(ret));
    return -1;
  }

  int epoll_fd = epoll_create1(0);
  if (epoll_fd == -1)
  {
    fprintf(stderr, "epoll_create1 failed at %s:%s  error is: ", __PRETTY_FUNCTION__, __LINE__, strerror(ret));
    return -1;
  }

  epoll_event event;
  event.data.fd = listen_fd;
  event.events = EPOLLIN | EPOLLET;
  ret = epoll_ctl (epoll_fd, EPOLL_CTL_ADD, listen_fd, &event);
  if (-1 == ret)
  {
    fprintf(stderr, "epoll_ctl failed at %s:%s  error is: ", __PRETTY_FUNCTION__, __LINE__, strerror(ret));
    return -1;
  }

  const int MAXEVENTS = 64;

  epoll_event* events = (epoll_event*) calloc(MAXEVENTS, sizeof event);

  std::vector<ctx::continuation> fibers;

  // Even loop.
  while (true)
  {
    int n_events = epoll_wait (epoll_fd, events, MAXEVENTS, -1);
    for (int i = 0; i < n_events; i++)
    {
      if ((events[i].events & EPOLLERR) ||
          (events[i].events & EPOLLHUP) ||
          (!(events[i].events & EPOLLIN)))
      {
        close (events[i].data.fd);
        closed_connection_handler(events[i].data.fd);
        
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
          ret = getnameinfo (&in_addr, in_len,
                             hbuf, sizeof(hbuf),
                             sbuf, sizeof(sbuf),
                             NI_NUMERICHOST | NI_NUMERICSERV);

          if (-1 == (ret = fcntl (infd, F_SETFL, fcntl(infd, F_GETFL, 0) | O_NONBLOCK)))
          {
            fprintf(stderr, "fcntl failed at %s:%s  error is: ", __PRETTY_FUNCTION__, __LINE__, strerror(ret));
            return -1;
          }

          event.data.fd = infd;
          event.events = EPOLLIN | EPOLLET;
          ret = epoll_ctl (epoll_fd, EPOLL_CTL_ADD, infd, &event);
          if (ret == -1)
          {
            fprintf(stderr, "epoll_ctl failed at %s:%s  error is: ", __PRETTY_FUNCTION__, __LINE__, strerror(ret));
            return -1;
          }

          if (fibers.size() < infd + 1)
            fibers.resize(infd + 1);

          struct end_of_file {};
          fibers[infd] = ctx::callcc([fd=infd, &fibers,&data_handler,
                                      closed_connection_handler,epoll_fd]
                                     (ctx::continuation&& sink) mutable {
              auto read = [&] (char* buf, int max_size) {

                ssize_t count = ::read(fd, buf, max_size);
                while (count <= 0)
                {
                  epoll_event event;
                  event.data.fd = fd;
                  event.events = EPOLLIN | EPOLLET;
                  epoll_ctl (epoll_fd, EPOLL_CTL_MOD, fd, &event);
                  sink = sink.resume();
                  count = ::read(fd, buf, max_size);
                  if ((count < 0 and errno != EAGAIN) or count == 0)
                    throw end_of_file();
                }
                return count;
              };

              auto write = [&] (const char* buf, int size) {
                const char* end = buf + size;
                ssize_t count = ::write(fd, buf, end - buf);
                if (count > 0) buf += count;
                while (buf != end)
                {
                  epoll_event event;
                  event.data.fd = fd;
                  event.events = EPOLLOUT | EPOLLET;
                  epoll_ctl (epoll_fd, EPOLL_CTL_MOD, fd, &event);
                  sink = sink.resume();
                  count = ::write(fd, buf, end - buf);
                  if (count > 0) buf += count;
                  if ((count < 0 and errno != EAGAIN) or count == 0)
                    throw end_of_file();
                }
              };
              
              try {
                data_handler(fd, read, write);
              }
              catch (end_of_file)
              {
                close(fd);
                closed_connection_handler(fd);                
              }
              return std::move(sink);
            });
          
        }
      }
      else // Data available on existing sockets.
      {
        fibers[events[i].data.fd] = fibers[events[i].data.fd].resume();
      }

    }
  }

  free(events);
  close(listen_fd);

  return 0;
}
