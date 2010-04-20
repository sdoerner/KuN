#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/ip.h>

#define PORT 5555
#define HOST "127.0.0.1"
#define BUFFER_SIZE 64

// fd 0 -> eingabe, 1-> ausgabe, 2 -> fehlerausgabe

void checkIfError(int result, char * errorMessage)
{
  if (-1 == result)
  {
    perror(errorMessage);
    exit(-1);
  }
}

int send_message(int sock, char* buffer)
{
  int len = read(0,buffer,BUFFER_SIZE);
  checkIfError(len,"Error reading from console");
  len = write(sock, buffer, len);
  checkIfError(len, "Error writing to socket");
}

int main (int argc, char ** argv)
{
  //create socket
  int sock = socket(PF_INET, SOCK_STREAM, 0);
  checkIfError(sock,"Error creating socket");

  //connect to target
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(PORT);
  inet_aton(HOST,&addr.sin_addr);
  int result = connect(sock, (struct sockaddr *) &addr, sizeof(addr));
  checkIfError(result,"Error connecting to socket");
  //send messages
  char buffer[BUFFER_SIZE];
  for (;;)
    send_message(sock, buffer);
  //close socket
  result = close(sock);
  checkIfError(result,"Error closing socket");
}
