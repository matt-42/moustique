# Moustique

Moustique is a tiny C++14 library (~180 LOC) providing an easy to use
interface to non-blocking network IO on linux.


# Implementation details

Moustique relies on linux epoll for non blocking IO and on
boost::context for resuming/suspending handlers.
The source code is rather small, fell free to dive into moustique.hh if you want
to know more about the implementation.

No dynamic allocations, no buffering is done by moustique internally. The read/write
callbacks are simply forwarding your buffer to the read/write syscalls.
The only overhead is the context switching of boost::context [1].

Dependencies: boost::context (-lboost_context), C++14.
Licence: MIT

## TCP echo example

```c++
#include "moustique.hh"

int main()
{
    moustique_listen(1234, // Port number
                     SOCK_STREAM, // TCP socket
                     2, // number of threads
         [] (int fd, auto read, auto write) {

           printf("new connection: %i\n", fd);

           char buf[1024];
           int received;
           
           while (received = read(buf, sizeof(buf))) // Suspend until new bytes
                                                     // are available for reading.

             if (!write(buf, received)) // Suspend until the socket is ready for a write.
               break;

           printf("end of connection: %i\n", fd);
         }
    );
}
```

## Compilation

```
g++/clang++ echo.cc -lboost_context -lpthread -DNDEBUG -O3
```


[1] https://www.boost.org/doc/libs/1_66_0/libs/context/doc/html/context/performance.html