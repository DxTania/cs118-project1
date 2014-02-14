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


using namespace std;

#define MAXCLIENTS 10
#define PORTNUM 15886

string readRequest(int connfd);
void* relayResponse(void* args);
int openConnectionTo(char* ip, int port);
void sendRequest (HttpRequest req, int sockfd);
void waitForClient();
void* acceptClient(void* connfd);
int setupServer(struct sockaddr_in server_addr);
char* getHostIP(HttpRequest req);

int threads[MAXCLIENTS];
int numChildren = 0;
int numConnections = 0;

typedef struct RequestInfo {
  int fd;
  pthread_t threadID;
} RequestInfo_t;

typedef struct ConnectionInfo {
  int serverfd;
  int clientfd;
  char* ip;
  int port;
} ConnectionInfo_t;

typedef struct CacheVal {
  string reponse;
  string time;
} CacheVal_t;

typedef struct relay_args {
    int serverfd;
    int clientfd;
    list<RequestInfo_t> *requests;
} RelayArgs_t;

map<string, CacheVal_t> cache;
list<ConnectionInfo_t> connections;

class ReadTimeout: public exception {
  virtual const char* what() const throw() {
    return "Read timed out";
  }
};

int main(void) {
  int listenfd = 0, connfd = 0;
  struct sockaddr_in server_addr;

  memset(&server_addr, 0, sizeof(server_addr));

  if ((listenfd = setupServer(server_addr)) == -1) {
    // Something went wrong
    return -1;
  }

  // Main listening loop
  for (;;) {
    if (numChildren < MAXCLIENTS) {
      // Set up client address struct
      struct sockaddr_in client_addr;
      memset(&client_addr, 0, sizeof(client_addr));
      socklen_t client_size = sizeof(client_addr);

      connfd = accept(listenfd, (struct sockaddr*) &client_addr,
        &client_size);

      // Set timeout for persistent connection closing to client
      struct timeval timeout;
      timeout.tv_sec = 20;
      timeout.tv_usec = 0;

      setsockopt (connfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
        sizeof(timeout));

      // Thread for each client
      pthread_t thread;
      int *arg = (int*) malloc(sizeof(*arg));
      *arg = connfd;
      pthread_create (&thread, 0, acceptClient, arg);
      numChildren++;
    }

    // Don't accept new connections until we have room
    while(numChildren == MAXCLIENTS) {}
  }

  return 0;
}

/**
 * Creates a connection to the server the HTTP request is asking for
 *
 * @return The socket file descriptor to the server
 */
int openConnectionTo(char* ip, int port) {
  // More socket fun :D
  struct sockaddr_in remote_addr;
  memset(&remote_addr, 0, sizeof(remote_addr));

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd == -1) {
    fprintf(stderr, "Failed to retrieve socket for remote\n");
    exit(1);
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
 * Attempts to send the request through the specified socket
 * Using a cache - HTTP Conditional Get
 */
void sendRequest (HttpRequest req, int sockfd, int clientfd, char* ip, int port) {
  // TODO: Include If-Modified-Since header with cached date
  // If not cached, send request without the If-Modified-Since header
  // Do not cache if cache-control is private (or no-cache?)
  // req.AddHeader("If-Modified-Since", "Wed, 29 Jan 2014 19:43:31 GMT");

  // Set up request for that server
  size_t bufsize = req.GetTotalLength() + 1;
  char* buf = (char*) malloc(bufsize);
  map<string, CacheVal_t>::iterator cachedResponse;
  string requestID;

  if (buf == NULL) {
    fprintf(stderr, "Failed to allocate buffer for request\n");
    exit(1);
  }
  req.FormatRequest(buf);

  string str;          //The string
  ostringstream temp;  //temp as in temporary
  temp << port;
  str=temp.str();

  requestID = req.GetHost() + req.GetPath() + str;
  // TODO: Add mutex!
  if ((cachedResponse = cache.find(requestID)) != cache.end())
  { // It is! Add header
    req.AddHeader("If-Modified-Since", ((cachedResponse->second).time).c_str());
    fprintf(stderr, "Found request in cache\n");
  }
  else { // It's not! Create entry to store response
    CacheVal_t val = {"", req.FindHeader("Date")};
    cache.insert(pair<string, CacheVal_t>(req.GetHost() + req.GetPath() + str, val));
    fprintf(stderr, "Date was %s\n", req.FindHeader("Date").c_str());
  }

  ConnectionInfo_t connection = {sockfd, clientfd, ip, port};
  connections.push_front(connection);

  // Write the request to the server & free buffer
  if (write(sockfd, buf, bufsize-1) < 0) {
    sockfd = openConnectionTo(ip, port);
    write(sockfd, buf, bufsize-1);
  }

  write(2, buf, bufsize-1);
  free(buf);
}

int connectionIsOpen(char* ip, int port, int clientfd) {
  for (list<ConnectionInfo_t>::iterator it = connections.begin(); it != connections.end(); it++) {
    int p = it->port;
    char* i = it->ip;
    int cfd = it->clientfd;
    if (strcmp(ip, i) == 0 && clientfd == cfd && p == port) {
      return it->serverfd;
    }
  }
  return -1;
}

/**
 * Attempt to process the client's HTTP request
 * Starts new thread to read responses
 */
void *acceptClient(void* connfdarg) {
  int connfd = *((int *) connfdarg), serverfd = -1;
  bool persistent = false;
  bool shouldClose = false, threadCreated = false;
  list<RequestInfo_t> requests;
  pthread_t thread;

  do {
    // Get next request from client
    string reqString;
    try {
      reqString = readRequest(connfd);
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

        char* ip = getHostIP(req);
        int port;
        if (req.GetPort() > 0) {
          port = req.GetPort();
        } else {
          port = 80;
        }

        serverfd = connectionIsOpen(ip, port, connfd);

        // Open connection with server if not open
        if (serverfd < 0) {
          serverfd = openConnectionTo(ip, port);
        }

        RequestInfo_t request = {serverfd, pthread_self()};
        requests.push_front(request);

        sendRequest(req, serverfd, connfd, ip, port);

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

        write(connfd, response.c_str(), response.length());
        shouldClose = true;
      }
    }

    // If we don't have a thread reading responses, create one
    if (!threadCreated && requests.size() > 0) {
      RelayArgs_t args = {serverfd, connfd, &requests};
      pthread_create (&thread, NULL, relayResponse, (void *)&args);
      threadCreated = true;
    } else {
      // Check if thread died
     if(pthread_tryjoin_np(thread, NULL) == 0) {
        break;
     }
    }
  } while(persistent && !shouldClose);

  fprintf(stderr, "Exiting\n");

  if (thread > 0) {
    pthread_join(thread, NULL);
  }
  close(serverfd);
  close(connfd);
  if (numConnections > 0) {
    numConnections--; // Closed file descriptors indicate closed connection
  }
  numChildren--;
  pthread_exit(0);
}

/**
 * Attempts to get an IP address for the provided hostname
 *
 * @return The IP address if found, NULL otherwise
 */
char* getHostIP(HttpRequest req) {
    // Get IP of host name
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
    return NULL;
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
 * Reads the response from the server
 * timeout @ 2 seconds -> check if we have a response return if we do
 * timeout @ 10 seconds -> return
 *
 * TODO: Why do we get a broken pipe if we wait to send the buffer until we have a full response?
 * Because this relays all responses....We have to tell when the server is done
 * with sending us a response before we relay it to the client using perhaps content length
 * Or realizing chunked responses?
 */
void* relayResponse(void *arguments) {
  struct relay_args *args = (struct relay_args *)arguments;
  int serverfd = args->serverfd;
  int clientfd = args->clientfd;
  // list<RequestInfo_t> *requests = args->requests;

   // Wait for all requests in front of us (we are the beginning) FIRST MAKE THIS FUNCITON ONLY RELAY ONE RESPONSE!!!

  // fprintf(stderr, "Waiting for threads ahead of us\n");
  // for (list<RequestInfo_t>::iterator relayIter = ++(*requests).begin(); relayIter != (*requests).end(); relayIter++) {
  //   if (relayIter->fd == serverfd) {
  //     pthread_join(relayIter->threadID, NULL);
  //   }
  // }

  bool headerParsed = false;
  size_t beginning = 0;
  size_t end = 0;

  string buffer;
  while (true) {
    char buf[1025];
    memset(&buf, 0, sizeof(buf));

    // Set server read timeout
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(serverfd, &readfds);
    struct timeval timeout;
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;
    select(serverfd+1, &readfds, NULL, NULL, &timeout);

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
      // TODO: check errno, due to timeout?
      break;
    } else if (numBytes == 0) {
      break;
    } else {
      //write(clientfd, buf, numBytes);
      buffer.append(buf);

      // CHUNKED OR CONTENT LENGTH
      if (!headerParsed) {
        // Parse entire header if we have it
        end = buffer.find("\r\n\r\n");
        if (end == string::npos) {
          continue;
        } else {
          // We have the full header
          string header = buffer.substr(beginning, end + 4);
          fprintf(stderr, "Header was %s\n", header);
        }

      }


    }
  }

  fprintf(stderr, "Buffer was %s\n", buffer.c_str());
// if (resp.GetStatusCode() == "304") { // Relevant cached response
//         // Is this the correct place?
//       }
//       else if () {// No cache control

//       }

  pthread_exit(0);
}

/**
 * Reads the request from the current client connection until
 * we receive the substring which ends HTTP requests \r\n\r\n
 *
 * @return The entire request we read, NULL if client has timed out
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