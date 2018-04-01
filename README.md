# epollpp

epollpp is a minimalist single header only C++ linux epoll wrapper.

Dependency: boost::context, C++14.

Licence: MIT.

## TCP echo example

```c++
#include "epollpp.hh"

int main()
{
    const char* port = "1234";
    epollpp_listen(1234,
         [] (int fd) { printf("new connection: %i\n", fd); },
         [] (int fd) { printf("lost connection: %i\n", fd); },
         [] (int fd, auto read, auto write) {
           char buf[1024];
           int received;
           
           while (received = read(buf, sizeof(buf))) // Yield until data reach the socket.
             write(buf, received); // Yield until the socket is ready for a new data write.
         }
         );
    });
}
```
