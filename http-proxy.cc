 /* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <netdb.h>

#include <list>
#include <map>

#include "http-response.h"
#include "http-request.h"

#include <exception>
#include <sstream>
#include <ctime>

using namespace std;

#define MAXCLIENTS 10
#define PORTNUM 15886

void* acceptClient(void* connfd);
int getCacheControl(string control);
char* getHostIP(HttpRequest req);
string getCacheString(HttpRequest req);
int openConnectionFor(HttpRequest req);
string readRequest(int connfd);
void relayResponse(int serverfd, int clientfd, HttpRequest req, string cachestring);
bool sendRequest (HttpRequest req, int sockfd, string cachestring);
int setupServer(struct sockaddr_in server_addr);

int numClients = 0;
int listenfd = 0;

typedef struct CacheVal {
  string response;
  string lastModified;
  string expires;
  time_t cachedDateSeconds;
  time_t maxAge;
} CacheVal_t;

map<string, CacheVal_t> cache;

class ReadTimeout: public exception {
  virtual const char* what() const throw() {
    return "Read timed out";
  }
};

void signalCallback(int signum) {
   close(listenfd);
   exit(1);
}

int main(void) {
  int connfd = 0;
  struct sockaddr_in server_addr;

  signal(1, signalCallback);
  signal(2, signalCallback);

  memset(&server_addr, 0, sizeof(server_addr));

  if ((listenfd = setupServer(server_addr)) == -1) {
    // Something went wrong
    return -1;
  }

  // Main listening loop
  for (;;) {
    if (numClients < MAXCLIENTS) {
      // Set up client address struct
      struct sockaddr_in client_addr;
      memset(&client_addr, 0, sizeof(client_addr));
      socklen_t client_size = sizeof(client_addr);

      connfd = accept(listenfd, (struct sockaddr*) &client_addr,
        &client_size);

      // Set timeout for persistent connection closing to client
      struct timeval timeout;
      timeout.tv_sec = 10;
      timeout.tv_usec = 0;

      setsockopt (connfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
        sizeof(timeout));

      // One thread for each client
      pthread_t thread;
      int *arg = (int*) malloc(sizeof(*arg));
      *arg = connfd;
      pthread_create (&thread, 0, acceptClient, arg);
      numClients++;
    }

    // Don't accept new connections until we have room
    while(numClients == MAXCLIENTS) {}
  }

  return 0;
}

/**
 * Attempts to send the request through the specified socket
 * Using a cache - HTTP Conditional Get when feasible
 *
 * @return bool False if we have a valid cache entry, True if we sent a request
 */
bool sendRequest (HttpRequest req, int sockfd, string cachestring) {
  map<string, CacheVal_t>::iterator cachedResponse;
  // Check if we even need to send the request
  if ((cachedResponse = cache.find(cachestring)) != cache.end()) {
    CacheVal_t cacheVal = (*cachedResponse).second;
    // Don't send a request if we don't have to
    time_t maxAge = cacheVal.maxAge;
    time_t timeCached = cacheVal.cachedDateSeconds;
    string expires = cacheVal.expires;
    string lastModified = cacheVal.lastModified;

    if (!expires.empty()) {
      // Check if the cache entry has expired this is LOCAL TIME! (Passes LA test)
      struct tm t;
      strptime(expires.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &t);
      time_t expiry = mktime(&t);
      time_t now = time(NULL);
      tm* gmtm = localtime(&now);
      time_t gmnow = mktime(gmtm);

      double seconds = difftime(expiry, gmnow);
      if (seconds > 0) {
        // Still valid due to expiry greater than now
        return false;
      }
    } else if (maxAge > 0 && timeCached + maxAge > time(NULL) ) {
      // Still valid (takes precedence over expires time)
      return false;
    }

    if (!lastModified.empty()) {
      // Else if we have last-modified send a conditional GET request
      req.AddHeader("If-Modified-Since", lastModified.c_str());
    }
  }

  // Get size of request and allocate buffer
  size_t bufsize = req.GetTotalLength() + 1;
  char* buf = (char*) malloc(bufsize);
  if (buf == NULL) {
    fprintf(stderr, "Failed to allocate buffer for request\n");
    exit(1);
  }

  // Write the request to the server & free buffer
  req.FormatRequest(buf);
  if (write(sockfd, buf, bufsize-1) < 0) {
    sockfd = openConnectionFor(req);
    write(sockfd, buf, bufsize-1);
  }

  free(buf);
  return true;
}

/**
 * Attempt to process the client's successive HTTP requests
 */
void *acceptClient(void* connfdarg) {
  int clientfd = *((int *) connfdarg);
  bool persistent = false;
  bool shouldClose = false;

  do {
    // Get next request from client
    string reqString;
    try {
      reqString = readRequest(clientfd);
    } catch (ReadTimeout e) {
      break;
    }
    if (reqString.length() > 0) {
      try {
        // Attempt to parse and send the request
        HttpRequest req;
        req.ParseRequest(reqString.c_str(), reqString.length());
        persistent = req.GetVersion() == "1.1";
        shouldClose = req.FindHeader("Connection").compare("close") == 0;

        int serverfd = openConnectionFor(req);

        string cachestring = getCacheString(req);
        if (!sendRequest(req, serverfd, cachestring)) {
          // We have a valid cache entry, send that to client instead
          string response = (*(cache.find(cachestring))).second.response;
          write(clientfd, response.c_str(), response.length());
          close(serverfd);
        } else {
          relayResponse(serverfd, clientfd, req, cachestring);
        }

      } catch (ParseException e) {
        // Catch exception and relay back to client
        string response;
        string not_implemented = "Request is not GET";
        fprintf(stderr, "%s\n", e.what());

        if (strcmp(e.what(), not_implemented.c_str()) == 0) {
          response = "501 Not Implemented\r\n\r\n";
        } else {
          response = "400 Bad Request\r\n\r\n";
        }

        write(clientfd, response.c_str(), response.length());
        shouldClose = true;
      }
    }
  } while(persistent && !shouldClose);

  close(clientfd);
  numClients--;
  pthread_exit(0);
}

/**
 * Reads and relays a response from server to client
 * If we get 304 Not Modified, we return the cache entry to the client
 */
void relayResponse(int serverfd, int clientfd, HttpRequest req, string cachestring) {
  string transferEncoding, buffer;
  size_t contentLength, contentRead = 0, beginning = 0, end = 0;
  bool headerParsed = false, isChunked = false, notModified = true;

  while (true) {
    char buf[1025];
    memset(&buf, 0, sizeof(buf));

    // Set server read timeout
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(serverfd, &readfds);
    struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
    select(serverfd + 1, &readfds, NULL, NULL, &timeout);

    // If server short times out, break if we have a full response
    if (!FD_ISSET(serverfd, &readfds)) {
      HttpResponse resp;
      try {
        resp.ParseResponse(buffer.c_str(), buffer.length());
        break;
      } catch (ParseException e) {
        // If we don't have a full response, wait for read to timeout
      }
    }

    int numBytes = read(serverfd, buf, sizeof(buf));
    if (numBytes < 0) {
      break;
    } else if (numBytes == 0) {
      break;
    } else {
      if (!notModified) {
        // Don't keep the client waiting for the response
        write(clientfd, buf, numBytes);
      }
      buffer.append(buf);

      if (!headerParsed) {
        // Parse entire header if we have it
        end = buffer.find("\r\n\r\n");
        if (end == string::npos) {
          // Haven't found the header yet
          continue;
        } else {
          // We have the full header, determine if content length or chunked by parsing
          string header = buffer.substr(beginning, end + 4);
          HttpResponse resp;
          resp.ParseResponse(header.c_str(), header.length());

          if (resp.GetStatusCode().compare("304") == 0) {
            break;
          } else {
            // Start writing to client
            notModified = false;
            write(clientfd, buffer.c_str(), buffer.length());
          }

          string cl = resp.FindHeader("Content-Length");
          if (cl.empty()) {
            cl = resp.FindHeader("Content-length");
          }
          transferEncoding = resp.FindHeader("Transfer-Encoding");
          if (cl.empty() && transferEncoding.compare("chunked") != 0) {
            fprintf(stderr, "Can't parse this response, no content length or chunked encoding\n%s\n",
              header.c_str());
          } else if (cl.empty()) {
            isChunked = true;
            if(buffer.find("0\r\n\r\n") != string::npos) {
              // We already got the whole chunk
              break;
            }
          } else {
            contentLength = atoi(cl.c_str());
            contentRead += buffer.substr(end + 4).size();
          }
          headerParsed = true;
        }
      } else {
        if (isChunked) {
          if(buffer.find("0\r\n\r\n") != string::npos) {
            // We got the whole response
            break;
          }
        } else {
          contentRead += numBytes;
          if (contentRead >= contentLength) {
            // We got the whole response
            break;
          }
        }
      } // end header was parsed
    } // end we read data
  } // while

  if (notModified) {
    // Send cached result back to client instead
    string response = (*(cache.find(cachestring))).second.response;
    write(clientfd, response.c_str(), response.length());

  } else {
    // Cache the response if possible by parsing its headers
    HttpResponse resp;
    resp.ParseResponse(buffer.c_str(), buffer.length());

    string cc = resp.FindHeader("Cache-Control");
    string ex = resp.FindHeader("Expires");
    int cacheControl = getCacheControl(cc);

    if ((!cc.empty() && cacheControl > 0) || !ex.empty()) {
      // We are allowed to cache this
      CacheVal_t val = {
        buffer,
        resp.FindHeader("Last-Modified"),
        resp.FindHeader("Expires"),
        time(NULL),
        cacheControl
      };

      pair<map<string, CacheVal_t>::iterator, bool> result =
        cache.insert( pair<string, CacheVal_t>( cachestring, val ));

      // Allow overwriting
      if (!result.second) {
        result.first->second = val;
      }
    } // We aren't allowed to cache this
  }

  close(serverfd);
}

/**
 * Creates a connection to the server the HTTP request is asking for
 *
 * @return The socket file descriptor to the server
 */
int openConnectionFor(HttpRequest req) {
  // More socket fun :D
  struct sockaddr_in remote_addr;
  memset(&remote_addr, 0, sizeof(remote_addr));

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd == -1) {
    fprintf(stderr, "Failed to retrieve socket for remote\n");
    exit(1);
  }

  char* ip = getHostIP(req);
  int port;
  if (req.GetPort() > 0) {
    port = req.GetPort();
  } else {
    port = 80;
  }

  // Connect to server that was requested
  remote_addr.sin_family = AF_INET;
  remote_addr.sin_addr.s_addr = inet_addr(ip);
  remote_addr.sin_port = htons(port);

  if (connect(sockfd, (struct sockaddr*) &remote_addr, sizeof(remote_addr)) == -1) {
    fprintf(stderr, "Failed to connect to remote server\n");
    exit(1);
  }

  // Server response full timeout
  struct timeval timeout;
  timeout.tv_sec = 10;
  timeout.tv_usec = 0;

  setsockopt (sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
    sizeof(timeout));

  return sockfd;
}

/**
 * Reads the request from the current client connection until
 * we receive the substring which ends HTTP requests \r\n\r\n
 *
 * @return The entire request we read
 */
string readRequest(int connfd) {
  string buffer;
  while (memmem(buffer.c_str(), buffer.length(), "\r\n\r\n", 4) == NULL) {
    char buf[1025];
    memset(&buf, 0, sizeof(buf));
    int numBytes = read(connfd, buf, sizeof(buf) -1);
    if (numBytes < 0) {
      fprintf(stderr, "Read request error or timeout\n");
      // TODO: Check errno, due to timeout?
      throw ReadTimeout();
      // break;
    } else if (numBytes == 0) {
      break;
    } else {
      buffer.append(buf);
    }
  }
  return buffer;
}

/**
 * Does necessary setup for the server including:
 * Creating, binding, and listening into a socket
 *
 * @return The socket we are successfully listening on
 */
int setupServer(struct sockaddr_in server_addr) {
  int one = 1, listenfd;

  if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    fprintf(stderr, "Failed to retrieve socket\n");
    return -1;
  }

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(PORTNUM);

  // TODO: Testing only? Allow reuse of socket quickly after we close it
  setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  if (bind(listenfd, (struct sockaddr*) &server_addr, sizeof(server_addr)) == -1) {
    fprintf(stderr, "Failed to bind to socket\n");
    return -1;
  }

  if(listen(listenfd, 20) == -1) {
    fprintf(stderr, "Failed to listen\n");
    return -1;
  }

  return listenfd;
}

/**
 * Attempts to get an IP address for the provided request
 *
 * @return The IP address if found, NULL otherwise
 */
char* getHostIP(HttpRequest req) {
  string hostname;
  if (req.GetHost().length() == 0) {
    hostname = req.FindHeader("Host");
  } else {
    hostname = req.GetHost();
  }

  struct hostent *he;
  struct in_addr **ip_addrs;

  if ((he = gethostbyname(hostname.c_str())) == NULL) {
    fprintf(stderr, "Couldn't get host by name\n");
    exit(1);
  }

  ip_addrs = (struct in_addr **) he->h_addr_list;
  char *ip = inet_ntoa(*ip_addrs[0]);

  if (ip == NULL) {
    fprintf(stderr, "Invalid host name\n");
    exit(1);
  }

  return ip;
}

/**
  * Parses cache-control string
  *
  * @return Max age of cache or 0 not allowed to cache
  */
int getCacheControl(string control){
  if(control.find("private")!=string::npos ||
    control.find("no-cache")!=string::npos ||
    control.find("no-store")!=string::npos) {
      return 0;
  }

  size_t maxage;
  if ((maxage = control.find("max-age")) != string::npos) {
    size_t start = control.find('=');
    if(start != string::npos){
      return atoi(control.substr(start + 1).c_str());
    }
  }

  return 0;
}


/**
 * Converts a request into a cache key
 *
 * @return The string using host, path, & port
 */
string getCacheString(HttpRequest req) {
  int p;
  if (req.GetPort() > 0) {
    p = req.GetPort();
  } else {
    p = 80;
  }

  ostringstream temp;
  temp << p;
  string port = temp.str();

  return req.GetHost() + req.GetPath() + port;
}