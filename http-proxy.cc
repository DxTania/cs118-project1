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
 
int main(void)
{
  int listenfd = 0,connfd = 0;
  
  struct sockaddr_in serv_addr;
 
  char sendBuff[1025];  

  fprintf(stderr, "Started proxy");
 
  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  //fprintf("socket retrieve success\n");
  
  memset(&serv_addr, '0', sizeof(serv_addr));
  memset(sendBuff, '0', sizeof(sendBuff));
      
  serv_addr.sin_family = AF_INET;    
  serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");//htonl(INADDR_ANY); 
  serv_addr.sin_port = htons(15010);    

  //fprintf("server addr is %d\n", serv_addr.sin_addr.s_addr);
 
  bind(listenfd, (struct sockaddr*)&serv_addr,sizeof(serv_addr));
  
  if(listen(listenfd, 10) == -1){
      fprintf(stderr, "Failed to listen\n");
      return -1;
  }
     
  
  connfd = accept(listenfd, (struct sockaddr*)NULL ,NULL); // accept awaiting request
  fprintf(stderr, "Connection accepted");

  strcpy(sendBuff, "Message from server");
  write(connfd, sendBuff, strlen(sendBuff));

  close(connfd);
  close(listenfd);
  return 0;
}