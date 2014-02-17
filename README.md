CS 118 Project 1 - HTTP Proxy
=============================

## Design Decisions

* Server
 - The server creates a new thread for each client, up to a maximum of MAXCLIENTS (20). Once we have reached MAXCLIENTS, the server will backlog 20 more clients to serve once other clients have been disconnected.

 * HTTP Requests
  - A single thread processes all succsesive HTTP requests from one client.
  - We use Stop & Go pipelining as one of the emails said we may use. We read a request, process the request with the server or cache, and then give the client the response.
  - Every time a request is about to be sent, we check the cache for any similar request (based on host, path, and port). If a cached entry exists and is not expired, we do not send a request to a server and we just give the client the response we have. If the cached entry has expired, we add an If-Modified-Since header with the cache entry's Last-Modified field.
  - We implement this for both the Expires header and the Cache-Control header (max-age)

  * HTTP Responses
  - We begin reading a response from the server after sending a request. Once we receive the header we check for 304 Not Modified. If not modified, we return the cached entry to the client. If the response is not 304 not modified, we check the header for either Content-Length or Transfer-Encoding "chunked" and continue to read the response until the appropriate ending, relaying each read from the server back to the client.
  - Once we receive a response, if we are allowed to cache it (Cache-Control or Expires) then we do add it to the cache, overwriting any previous cached entry for the same request.

## Other Notes to TA

* http-tester.py passes all tests
* http-tester-conditionalGET-LAtime.py passes all tests
 - This one seems slightly inconsistent due to something with times in sendRequest (Expiry and now)
 - Unsure if it may have been because I forgot to restart the server
* Must restart server in bewteen the tests because the cached value from first test interferes
* I emailed you about someone forking our repository. If it seems as though someone else has similar code, I can make the following github repo: https://github.com/DxTania/cs118-project1 public again so I may prove this code is ours.
* There are timeouts for the following:
 - Server read timeout - 10 seconds
 - Client read timeout - 10 seconds
 - Server read short timeout of 5 seconds (using select) in relayResponse
