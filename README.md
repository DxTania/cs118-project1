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

You should be able to send an HTTP request and get a response (in the end), right now since carraige returns are annoying to get into telnet typing STOP also closes the connection.

## TODO:

* Start actually processing the HTTP requests (sendRequest)
* Caching
* Connections that could possibly be waiting, are they queued?