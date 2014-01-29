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

#include "http-response.h"
#include "http-request.h"

using namespace std;

#define MAXPROCESSES 2

string readRequest(int connfd);
void recordChild(pid_t pid, bool unrecord);
void waitForClient();
void acceptClient(int connfd);
int setupServer(struct sockaddr_in server_addr);

int pids[MAXPROCESSES];
int numChildren = 0;

int main(void) {
  int listenfd = 0, connfd = 0;
  struct sockaddr_in server_addr;

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

      fprintf(stderr, "Waiting to accept\n");
      connfd = accept(listenfd, (struct sockaddr*) &client_addr, &client_size);

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
 * Does necessary setup for the server including:
 * Creating, binding, and listening into a socket
 *
 * @return The socket we are listening in on
 */
int setupServer(struct sockaddr_in server_addr) {
	int one = 1, listenfd = socket(AF_INET, SOCK_STREAM, 0);
  fprintf(stderr, "Socket retrieved successfully\n");

  memset(&server_addr, 0, sizeof(server_addr));
  memset(&pids, 0, sizeof(pids));

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
 * Child process only:
 * Attempt to process the client's HTTP request
 */
void acceptClient(int connfd) {
	HttpRequest req;
  string reqString = readRequest(connfd);

  try {
    req.ParseRequest(reqString.c_str(), reqString.length());
    fprintf(stderr, "Request was of type %d\n", req.GetMethod());

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
 * Blocks until we can reap a child process
 * and unrecords the child process from our table
 */
void waitForClient() {
	int status;
  pid_t pid = waitpid(-1, &status, 0);

  if (pid > 0 && WIFEXITED(status)) {
  	fprintf(stderr, "A child %d exited\n", pid);
		recordChild(pid, false);
  }
}

/**
 * Reads the request from the current client connection until
 * we receive the substring which ends HTTP requests \r\n\r\n
 *
 * @return The entire request we read
 */
string readRequest(int connfd) {
	string buffer;
  while (memmem(buffer.c_str(), buffer.length(), "\r\n\r\n", 4) == NULL
    && memmem(buffer.c_str(), buffer.length(), "STOP", 4) == NULL) { // easy testing
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
