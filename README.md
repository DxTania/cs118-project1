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
```
  - If we get a request
    -- If the request exists in the cache
        Check the response for Last-Modified
        Send request to server with If-Modified-Since header
        If we get 304 Not Modified
          return Response in cache to client
        Else
          return Response to client and update cache
    -- Else
        Send the request to server
        Check the response header for Cache-control or similar
        If we can cache the response
          update cache
        Else
          Just return the response to the client
```
* Queued connections, are they taken care of?
* Persistent connection to us, if requesting files from diff site we need to open new connections? (can clients ask for files from diff sites in same connection?)
* Other random TODOs in http-proxy.cc

Passes first three tests! Basic object fetching, persistent connection, and concurrent connections.


Will have to devise a method of determining when the server is done sending a response in the relayResponse() method as well as implement checking the
response for 304 not modified, and implement sending If-Modified-Since in sendRequest.


### MORE TODO

* Up to 100 simultaneous connections to 100 different servers

* Keep track of how many connections we have opened
* Keep list of ip/port/fd we are connected to

* If we get a request we have a connection to
- send request to corresponding fd

* Else
- Connect to server and send request and mark ip/port/serverfd/CLIENTFD

* if we don't get a request for a server, timeout and close connection to server and decrement connections (make sure if they close on us we decrement connections too)

* Maybe change openConnectionFor to take an ip address instead
- Calculate ip after parserequest to check if we have connection

* Make relayResponse return after receiving exactly one response
- If we get a request we have a connection to, send the request and add to queue
- If we get a request we don't have connection to, open connection, send request, add to queue
- For each request, call relayResponse with new thread, thread should not start reading until previous threads in queue w/ SAME serverFD are done reading, thread should not start writing until all previous threads in queue with same CLIENTFD have finished
- If server closes on us, close back
- Timeout on writing to any single server and close if ANY client has not written to that server in time

* What about seperate clients talking to same server?
- Open a seperate connection to the server!