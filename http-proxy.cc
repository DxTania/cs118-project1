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

#include "http-response.h"
#include "http-request.h"

#include <exception>

using namespace std;

#define MAXCLIENTS 10
#define PORTNUM 15886

string readRequest(int connfd);
void* relayResponse(void* args);
int openConnectionFor(HttpRequest req);
void sendRequest (HttpRequest req, int sockfd);
void waitForClient();
void* acceptClient(void* connfd);
int setupServer(struct sockaddr_in server_addr);
char* getHostIP(string hostname);

int threads[MAXCLIENTS];
int numChildren = 0;

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
int openConnectionFor(HttpRequest req) {
  // More socket fun :D
  struct sockaddr_in remote_addr;
  memset(&remote_addr, 0, sizeof(remote_addr));

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd == -1) {
    fprintf(stderr, "Failed to retrieve socket for remote\n");
    exit(1);
  }

  // Get IP of host name
  char* ip;
  if (req.GetHost().length() == 0) {
    ip = getHostIP(req.FindHeader("Host"));
  } else {
    ip = getHostIP(req.GetHost());
  }

  if (ip == NULL) {
    fprintf(stderr, "Invalid host name\n");
    exit(1);
  }

  // Connect to server that was requested
  remote_addr.sin_family = AF_INET;
  remote_addr.sin_addr.s_addr = inet_addr(ip);
  if (req.GetPort() > 0) {
    remote_addr.sin_port = htons(req.GetPort());
  } else {
    remote_addr.sin_port = htons(80);
  }

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
void sendRequest (HttpRequest req, int sockfd) {
  // TODO: Include If-Modified-Since header with cached date
  // If not cached, send request without the If-Modified-Since header
  // Do not cache if cache-control is private (or no-cache?)
  // req.AddHeader("If-Modified-Since", "Wed, 29 Jan 2014 19:43:31 GMT");

  // Set up request for that server
  size_t bufsize = req.GetTotalLength() + 1;
  char* buf = (char*) malloc(bufsize);
  if (buf == NULL) {
    fprintf(stderr, "Failed to allocate buffer for request\n");
    exit(1);
  }
  req.FormatRequest(buf);

  // Write the request to the server & free buffer
  fprintf(stderr, "Sent request:\n%s", buf);
  write(sockfd, buf, bufsize-1);
  free(buf);
}

struct relay_args {
    int serverfd;
    int clientfd;
};

/**
 * Attempt to process the client's HTTP request
 * Starts new thread to read responses
 */
void *acceptClient(void* connfdarg) {
  int serverfd = -1, connfd = *((int *) connfdarg);
  bool persistent = false, connectionOpen = false;
  bool shouldClose = false, threadCreated = false;
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

        // Open connection with server if not open
        if (!connectionOpen) {
          serverfd = openConnectionFor(req);
          connectionOpen = true;
        }
        sendRequest(req, serverfd);

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
    if (!threadCreated) {
      struct relay_args args;
      args.serverfd = serverfd;
      args.clientfd = connfd;
      pthread_create (&thread, NULL, relayResponse, (void *)&args);
      threadCreated = true;
    } else {
      // Check if thread died
     if(pthread_tryjoin_np(thread, NULL) == 0) {
        break;
     }
    }
  } while(persistent && !shouldClose);

  pthread_join(thread, NULL);
  close(serverfd);
  close(connfd);
  numChildren--;
  pthread_exit(0);
}

/**
 * Attempts to get an IP address for the provided hostname
 *
 * @return The IP address if found, NULL otherwise
 */
char* getHostIP(string hostname) {
  struct hostent *he;
  struct in_addr **ip_addrs;

  if ((he = gethostbyname(hostname.c_str())) == NULL) {
    fprintf(stderr, "Couldn't get host by name\n");
    return NULL;
  }

  ip_addrs = (struct in_addr **) he->h_addr_list;
  return inet_ntoa(*ip_addrs[0]);
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
      write(clientfd, buf, numBytes);
      buffer.append(buf);
    }
  }
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