# Moustique

Moustique provide a coroutine-based interface to non-blocking/asyncronous read write operation
on linux sockets.

Dependency: boost::context (-lboost_context), C++14.

Licence: MIT.

## TCP echo example

```c++
#include "moustique.hh"

int main()
{
    const char* port = "1234";
    moustique_listen(port,
         [] (int fd) { printf("lost connection: %i\n", fd); },
         [] (int fd, auto read, auto write) {
           printf("new connection: %i\n", fd);
           char buf[1024];
           int received;
           
           while (received = read(buf, sizeof(buf))) // Yield until new bytes
                                                     // get read from the socket.

             write(buf, received); // Yield until the socket is ready for a new data write.
         }
    );
}
```

### Comparison with boost::asio TCP echo example [1]

    - 5.5x less code to write.
    - 7.5x faster compilation (6.1s vs 0.8s).
    - 24x smaller binary size (20Kb vs 475Kb).
    - Same performances.

[1] https://www.boost.org/doc/libs/1_65_1/doc/html/boost_asio/example/cpp11/spawn/echo_server.cpp
