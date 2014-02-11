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
* Close persistent connections after some timeout, is this usual way?
* What is the persistent connection test looking for? immediate closing after 1st test?
* are we supposed to support Connection: close header?
* persistent connection to us, if requesting files from diff site we need to open new connections? (can clients ask for files from diff sites in same connection?)

Right now, we pass basic object fetching.
Persistent connection test passes if we delete the basic object fetching code.. better than nothing?  =/


### Ideas:


How to get around a server that doesn't support concurrent connections like in the test script?

close connection if we have read a full response & have not sent any more requests!!

have second child wait more than the first child for the response

have second child know how many responses it is waiting for?

while reading response, store in buffer

if we can parse a http request in that buffer

kill the child process

if child process was killed AND we got no more requests, close all the connections

when the child process dies (have parent check if dead)

spawn a new one!

if children send requests and main process receives responses
child process can signal parent when done receiving requests on some timeout!!

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

GET http://127.0.0.1:21271/basic HTTP/1.1
Host: 127.0.0.1:21271
Accept-Encoding: identity