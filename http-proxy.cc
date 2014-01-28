/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>

#include "http-request.h"

using namespace std;

int main(void)
{
  int listenfd = 0, connfd = 0;
  struct sockaddr_in server_addr;
  string buffer;
  HttpRequest req;
 
  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  fprintf(stderr, "socket retrieve success\n");
  
  memset(&server_addr, '0', sizeof(server_addr));
      
  server_addr.sin_family = AF_INET;    
  server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");//htonl(INADDR_ANY); 
  server_addr.sin_port = htons(15016);    
 
  bind(listenfd, (struct sockaddr*) &server_addr, sizeof(server_addr));
  
  if(listen(listenfd, 10) == -1){
      fprintf(stderr, "Failed to listen\n");
      return -1;
  }
     
  for (;;) {
    connfd = accept(listenfd, (struct sockaddr*)NULL ,NULL); // accept awaiting request
    fprintf(stderr, "Connection accepted\n");

    // Search for substring that ends the HTTP request
    while (memmem(buffer.c_str(), buffer.length(), "\r\n\r\n", 4) == NULL) {
      char buf[1025];
      if (read(connfd, buf, sizeof(buf) - 1) < 0) {
        fprintf(stderr, "Read error\n");
      } else {
        buf[sizeof(buf) - 1] = 0;
        buffer.append(buf);
      }
    }

    try {
      req.ParseRequest(buffer.c_str(), buffer.length());
    } catch (ParseException e) {
      fprintf(stderr, "%s\n", e.what());
      close(connfd);
      close(listenfd);
      return 0;
    }
    fprintf(stderr, "Request was of type %d", req.GetMethod());

    close(connfd);
    break;
  }
  
  close(listenfd);
  return 0;
}
