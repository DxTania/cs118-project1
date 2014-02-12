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

#define MAXPROCESSES 10
#define PORTNUM 15886

string readRequest(int connfd);
void relayResponse(int serverfd, int clientfd);
int openConnectionFor(HttpRequest req);
void sendRequest (HttpRequest req, int sockfd);
void recordChild(pid_t pid, bool record);
void waitForClient();
void acceptClient(int connfd);
int setupServer(struct sockaddr_in server_addr);
char* getHostIP(string hostname);

int pids[MAXPROCESSES];
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
  memset(&pids, 0, sizeof(pids));

  if ((listenfd = setupServer(server_addr)) == -1) {
    // Something went wrong
    return -1;
  }

  // Main listening loop
  for (;;) {
    if (numChildren < MAXPROCESSES) {
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

      // Fork a process to take care of the client
      pid_t pid = fork();
      if (pid < 0) {
        fprintf(stderr, "Fork failed\n");
        return -1;

      } else if (pid == 0) {
        acceptClient(connfd);

      } else {
        // We had space to run another process, record it
        recordChild(pid, true);
      }
    }

    // Don't accept new connections until we have room
    if (numChildren == MAXPROCESSES) {
      waitForClient();
    }
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

/**
 * Child process only:
 * Attempt to process the client's HTTP request
 * Forks a new process to read responses
 */
void acceptClient(int connfd) {
  bool persistent = false;
  bool connectionOpen = false;
  bool shouldClose = false;
  int serverfd = -1;
  pid_t pid = -1;

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

    // If we don't have a process reading responses, fork one
    if (pid < 0) {
      pid = fork();
      if (pid < 0) {
        fprintf(stderr, "Fork failed\n");
        break;
      } else if (pid == 0) {
        relayResponse(serverfd, connfd);
        _exit(0);
      }
    } else {
      // See if process is done reading responses
      int status;
      waitpid(pid, &status, WNOHANG);
      if (WIFEXITED(status)) {
        break;
      }
    }
  } while(persistent && !shouldClose);

  close(serverfd);
  close(connfd);
  _exit(0);
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
void relayResponse(int serverfd, int clientfd) {
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
  fprintf(stderr, "Got response:\n%s\n\n", buffer.c_str());
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
 * Record the pid of child process in our pid table
 * If record is false, sets the space of the pid to 0
 */
void recordChild(pid_t pid, bool record) {
  for (int i = 0; i < MAXPROCESSES; i++) {
    if (pids[i] == record? 0 : pid) {
      pids[i] = record? pid : 0;
      record ? numChildren++ : numChildren--;
      break;
    }
  }
  return;
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
 * Blocks until we can reap a child process
 * and unrecords the child process from our table
 *
 * TODO: Error handling for children that don't exit correctly
 * TODO: make sure all children actually exit at appropriate times
 *
 * TODO: Does this screw up waitpid for acceptClient?
 */
void waitForClient() {
  int status;
  int found = false;
  pid_t pid;

  while(!found) {
    pid = waitpid(-1, &status, 0);
    for(int i = 0; i < MAXPROCESSES; i++) {
      if (pids[i] == pid) {
        found = true;
      }
    }
  }

  if (pid > 0 && WIFEXITED(status)) {
    recordChild(pid, false);
  }
}