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

using namespace std;

#define MAXPROCESSES 2

string readFromSocket(int connfd);
void recordChild(pid_t pid, bool record);
void waitForClient();
void acceptClient(int connfd);
int setupServer(struct sockaddr_in server_addr);
char* getHostIP(string hostname);

int pids[MAXPROCESSES];
int numChildren = 0;

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

      connfd = accept(listenfd, (struct sockaddr*) &client_addr, &client_size);

      // Fork a process to take care of the client
      pid_t pid = fork();
      if (pid < 0) {
        fprintf(stderr, "Fork failed\n");
        return -1;

      } else if (pid == 0) {
        acceptClient(connfd);

      } else {
        // We had space to run another process, record it
        fprintf(stderr, "Connection accepted by child %d\n", pid);
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
 * TODO: Finish this
 * Attempts to send the request and retrieve a response
 * Using a cache - HTTP Conditional Get
 */
void sendRequest (HttpRequest req) {
  // More socket fun :D
  struct sockaddr_in remote_addr;
  memset(&remote_addr, 0, sizeof(remote_addr));

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd == -1) {
    fprintf(stderr, "Failed to retrieve socket for remote\n");
    return;
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
    return;
  } else {
    fprintf(stderr, "Connecting to IP: %s\n\n", ip);
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
    return;
  }

  // TODO: Include If-Modified-Since header with cached date
  // If not cached, send request without the If-Modified-Since header
  // Do not cache if cache-control is private (or no-cache?)
  req.AddHeader("If-Modified-Since", "Wed, 29 Jan 2014 19:43:31 GMT");

  // Set up request for that server
  size_t bufsize = sizeof(char) * req.GetTotalLength();
  char* buf = (char*) malloc(bufsize);
  if (buf == NULL) {
    fprintf(stderr, "Failed to allocate buffer for request\n");
    return;
  }
  memset(buf, 0, bufsize);
  req.FormatRequest(buf);

  // Write the request to the server
  write(sockfd, buf, bufsize);

  // Wait for response from the server
  HttpResponse resp;
  string answer = readFromSocket(sockfd);
  resp.ParseResponse(answer.c_str(), answer.length());

  fprintf(stderr, "Details about the response:\n");
  fprintf(stderr, "%s\n", answer.c_str());

  // Cache the response if needed

  free(buf);
}

/**
 * Child process only:
 * Attempt to process the client's HTTP request
 */
void acceptClient(int connfd) {
  HttpRequest req;
  string reqString = readFromSocket(connfd);

  try {
    req.ParseRequest(reqString.c_str(), reqString.length());
    sendRequest(req);

  } catch (ParseException e) {
    // Bad request, send appropriate error & close the connection
    string response;
    string not_implemented = "Request is not GET";
    fprintf(stderr, "%s\n", e.what());

    if (strcmp(e.what(), not_implemented.c_str()) == 0) {
      response = "501 Not Implemented\r\n\r\n";
    } else {
      response = "400 Bad Request\r\n\r\n";
    }

    write(connfd, response.c_str(), response.length());
  }
  close(connfd);
  _exit(0);
}

/**
 * Attempts to get an IP address for the provided hostname
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
 * Reads the request from the current client connection until
 * we receive the substring which ends HTTP requests \r\n\r\n
 *
 * @return The entire request we read
 */
string readFromSocket(int connfd) {
  string buffer;
  while (memmem(buffer.c_str(), buffer.length(), "\r\n\r\n", 4) == NULL) {
    char buf[1025];
    memset(&buf, 0, sizeof(buf));
    if (read(connfd, buf, sizeof(buf) - 1) < 0) {
      fprintf(stderr, "Read error\n");
    } else {
      buf[sizeof(buf) - 1] = 0;
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
  server_addr.sin_port = htons(14886);

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
 */
void waitForClient() {
  int status;
  pid_t pid = waitpid(-1, &status, 0);
  if (pid > 0 && WIFEXITED(status)) {
    recordChild(pid, false);
  }
}