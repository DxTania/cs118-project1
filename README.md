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

You should be able to send an HTTP request and get a response (in the end), I added a file called carraige returns that can be copy and pasted into telnet to actually send a request.

## TODO:

* Caching
  - Figure out what to store cache in
  - Send If-Modified-Since headers
  - Only cache if Cache-Control is not private?
  - If we get Not Modified, return cache result to user
  - Otherwise we update the cache and return that result to user
  - Also: Support both persistent & non-persistent connections
* Connections that could possibly be waiting, are they queued?
