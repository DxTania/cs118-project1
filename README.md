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

Right now, we pass basic object fetching.
Persistent connection test passes if we delete the basic object fetching code.. better than nothing?  =/
