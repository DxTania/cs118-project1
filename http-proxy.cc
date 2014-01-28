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
void recordChild(pid_t pid);

int pids[MAXPROCESSES];

int main(void) {

  int listenfd = 0, connfd = 0, numChildren = 0, one = 1;
  struct sockaddr_in server_addr;

  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  fprintf(stderr, "Socket retrieved successfully\n");

  memset(&server_addr, 0, sizeof(server_addr));
  memset(&pids, 0, sizeof(pids));

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(14886);

  // Testing only? Allow reuse of socket quickly after we close the server
  setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  if (bind(listenfd, (struct sockaddr*) &server_addr, sizeof(server_addr)) == -1) {
    fprintf(stderr, "Failed to bind to socket\n");
    return -1;
  }

  if(listen(listenfd, 20) == -1) {
    fprintf(stderr, "Failed to listen\n");
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
      	//child process
        fprintf(stderr, "Connection accepted\n");
        string request = readRequest(connfd);

        // Try to parse the request we just received
        HttpRequest req;
        try {
          req.ParseRequest(request.c_str(), request.length());
          fprintf(stderr, "Request was of type %d\n", req.GetMethod());
        } catch (ParseException e) {
          // Bad request, send error & close the connection
          string response;
          string not_implemented = "Request is not GET";
          fprintf(stderr, "%s\n", e.what());
          if (strcmp(e.what(), not_implemented.c_str()) == 0) {
            response = "501 Not Implemented\r\n\r\n";
          } else {
            response = "400 Bad Request\r\n\r\n";
          }
          write(connfd, response.c_str(), response.length());
          fprintf(stderr, "Wrote response to client, shoud close connection now\n");
          close(connfd);
          _exit(0);
        }

        close(connfd);
        _exit(0);
      } else {
      	// We had space to run another process, record it
      	recordChild(pid);
				numChildren++;
      }
    }

    if (numChildren == MAXPROCESSES) {
	  	// If we ran out of processes wait on any child
	    int status;
	    fprintf(stderr, "Waiting for a child to finish\n");
	    for (int i = 0; i < MAXPROCESSES; i++) {
	    	if (pids[i] > 0) {
	    		waitpid(pids[i], &status, WNOHANG);
		    	if (!WIFEXITED(status)) {
		    		fprintf(stderr, "A child exited\n");
		    		numChildren--;
		    		pids[i] = 0;
		    		break;
		    	}
	    	}
	    }
	  }
    // parent process
    // accept more incoming connections
  }

  close(listenfd);
  return 0;
}

string readRequest(int connfd) {
	// Keep reading until substring that ends the HTTP request appears
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

void recordChild(pid_t pid) {
	for (int i = 0; i < MAXPROCESSES; i++) {
		if (pids[i] == 0) {
			pids[i] = pid;
			break;
		}
	}
	return;
}
