CS 118 Project 1 - HTTP Proxy
=============================

Partners:

Tania DePasquale

Julian Brown

## To start proxy server:

```
./waf

build/http-proxy
```

## To test:

Use telnet!

First run the proxy server, then run the following telnet command

```
telnet 127.0.0.1 14886
```

You should be able to send an HTTP request and get a response.

## TODO:

* Caching
  - Figure out what to store cache in
  - Send If-Modified-Since headers
  - Only cache if Cache-Control is not private?
  - If we get Not Modified, return cache result to user
  - Otherwise we update the cache and return that result to user
* Connections that could possibly be waiting, are they queued or auto acepted?
* Are we supposed to support Connection: close header?
* Persistent connection to us, if requesting files from diff site we need to open new connections? (can clients ask for files from diff sites in same connection?)

Passes first three tests! Basic object fetching, persistent connection, and concurrent connections.


### Ideas:


### Timeouts and waiting:
```
fd_set readfds;
FD_ZERO(&readfds);
FD_SET(serverfd, &readfds);
struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
select(serverfd+1, &readfds, NULL, NULL, &timeout);
```
```
struct timeval timeout;
timeout.tv_sec = 2;
timeout.tv_usec = 0;

setsockopt (connfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
            sizeof(timeout));
```