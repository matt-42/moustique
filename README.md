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

### Comparison with boost::asio TCP echo example

    - 6x less code to write.
    - 3.5x faster compilation (2.61s vs 0.75s).
    - 6.25x smaller binary size (20Kb vs 125Kb).
    - Same performances.
