/**
 * \file httpd.c
 * \brief A basic web server
 */

#include "util.h"

#include <fcntl.h>
#include <getopt.h>
#include <netdb.h> /* addrinfo */
#include <netinet/ip.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h> /* getopt */

/** \brief Size of input buffers */
#define BUFFER_SIZE 1024
/** \brief maximal size of requestable urls */
#define MAX_URL_SIZE 256
/** \brief maximal size of the absolute path of any file to be delivered */
#define MAX_FILE_PATH_SIZE MAX_URL_SIZE + 29
/** \brief document root of the web server (where the web files are located) */
const char documentRoot[] = "/home/sdoerner/svn/KuN/htdocs";
/** \brief Set if we want to enable debug output. */
/*#define DEBUG*/
/** \brief Number of file descriptors to check when calling poll */
#define FDCOUNT 2
/** \brief Maximal number of active connections */
#define MAXCON 10

/** \brief The status of a connection */
typedef enum
{
  statusUnused, 
  statusOpen
} statusType;

struct connectionType
{
  statusType status;
  int fileFd;
  int socketFd;
  char buffer[BUFFER_SIZE];
  int bufferFreeOffset;
};

/** \brief The only open socket at any time (almost). */
int listeningSocket = -1;

/**
 * All active communication connections
 */
struct connectionType connections[MAXCON];

/**
 * Frees allocated ressources on exiting the program.
 * Is to be registered as a callback using atexit.
 */
void cleanUpOnExit()
{
  /* try to close the socket if necessary */
  if (listeningSocket != -1)
  {
  #ifdef DEBUG
    puts("Closing Socket on Exit.");
  #endif
    int result = close(listeningSocket);
    if (result == -1)
      perror("Error closing Socket");
  }
  if (connections[0].status == statusOpen)
  {
    close (connections[0].socketFd);
    if (connections[0].fileFd!=-1)
      close(connections[0].fileFd);
  }
  fflush(stdout);
}

/**
 * Checks the return value \a result and prints the last error message if it
 * indicates errors.
 *
 * \param result Result value to evaluate
 * \param errorMessage String to prepend to the printed error message
 */
void exitIfError(int result, char * errorMessage)
{
  if (-1 == result)
  {
    perror(errorMessage);
    exit(1);
  }
}

/**
 * Sends the requested file over the connection.
 * \param connection The connection that requested the file. Contains the target and requested file.
 */
void sendFile(struct connectionType * const connection)
{
  int len = read(connection->fileFd, connection->buffer, BUFFER_SIZE);
  exitIfError(len,"Error reading from file");
  while (len>0)
  {
    len = write(connection->socketFd, connection->buffer, len);
    exitIfError(len, "Error writing to socket");
    len = read(connection->fileFd, connection->buffer, BUFFER_SIZE);
    exitIfError(len,"Error reading from file");
  }
}

void sendMessage(struct connectionType * const connection, const char * message)
{
  exitIfError(write(connection->socketFd, message, strlen(message)), "Error writing to socket");
}

void sendHeaders(struct connectionType * const connection, int statusCode)
{
  switch (statusCode)
  {
    case 200:
      sendMessage(connection, "HTTP/1.0 200 OK\r\n");
      time_t currentSeconds = time (NULL);
      struct tm * currentGMT = gmtime(&currentSeconds);
      char dateMessage[40];
      if (sprintf(dateMessage,"Date: %s, %.2d %s %.4d %.2d:%.2d:%.2d GMT\r\n", weekDays[currentGMT->tm_wday], currentGMT->tm_mday, months[currentGMT->tm_mon], 1900 + currentGMT->tm_year, currentGMT->tm_hour, currentGMT->tm_min, currentGMT->tm_sec)<0)
      {
        fputs("Error creating dateMessage", stderr);
        exit(1);
      }
      sendMessage(connection, dateMessage);
      break;
    case 404:
      sendMessage(connection, "HTTP/1.0 404 Not Found\r\n");
      break;
    default:
       return;
  }
  sendMessage(connection, "\r\n");
}

/**
 * Receive a string message through a socket.
 * \param sock Socket descriptor for the socket to receive the message through.
 * \param buffer Buffer for buffering the message we receive.
 * \param size Size of the \a buffer.
 */
int receiveMessage(int sock, char* buffer, int size)
{
  int len = read(sock, buffer, size);
  exitIfError(len,"Error reading from socket");
  return len;
}

/**
 * Closes a given connection.
 * \param connection The connection to close.
 */
void closeConnection(struct connectionType * const connection)
{
  if (close(connection->socketFd) == -1)
    fputs("Error closing socket", stderr);
  connection->socketFd = -1;
  if (close(connection->fileFd) == -1)
    fputs("Error closing file", stderr);
  connection->fileFd = -1;
  connection->status = statusUnused;
}

/**
 * Parses a HTTP request and extracts the name of the requested file.
 * \param buffer Contains the HTTP request.
 * \param result Array in which the resulting file name is to be written.
 * \param resultlength Size of the array \a result.
 * \returns The name of the requested file.
 */
char * parseRequest(char* buffer, char * result, int resultlength)
{
  const char delimiters[] = "\n\r";
  char * tokenStart = strtok(buffer, delimiters);
  while (tokenStart != 0)
  {
#ifdef DEBUG
    puts(tokenStart);
#endif
    if (strncmp(tokenStart, "GET", 3) == 0)
    {
      tokenStart+=4;
      const char* urlEnd = strchr(tokenStart, ' ');
      if (urlEnd == 0)
      {
        fprintf(stderr, "Error: Format of the GET header is invalid.");
        exit(1);
      }
      int urlLength = min(resultlength-1, urlEnd - tokenStart);
      strncpy(result, tokenStart, urlLength);
      result[urlLength] = '\0';
    }
    tokenStart  = strtok((char *)0, delimiters);
  }
  return (char *) result;
}

/**
 * Processes and answers a client request.
 * \param connection The connection of the client to be served.
 */
void processRequest(struct connectionType * const connection)
{
  /* receive request */
  int length;
  for (;;)
  {
#ifdef DEBUG
    printf("length: %d\n", connection->bufferFreeOffset);
#endif
    length = receiveMessage(connection->socketFd, connection->buffer + connection->bufferFreeOffset, BUFFER_SIZE - connection->bufferFreeOffset);
    if (length == 0)
    {
      fprintf(stderr, "Error: Connection closed by client");
      exit(1);
    }
    else
    {
      connection->bufferFreeOffset += length;
      connection->buffer[connection->bufferFreeOffset]='\0';
      if (0!=strstr(connection->buffer, "\r\n\r\n"))
        break;
    }
#ifdef DEBUG
    puts("!!!! First packet didn't contain double newline !!!!");
#endif
  }
  /* process it */
  char url[MAX_URL_SIZE];
  parseRequest(connection->buffer, url, MAX_URL_SIZE);
  /* answer it */
  char filepath[MAX_FILE_PATH_SIZE];
  memset(filepath, 0, sizeof(filepath));
  strncpy(filepath, documentRoot, strlen(documentRoot));
  strncpy(filepath + strlen(documentRoot), url, strlen(url));
#ifdef DEBUG
  puts(url);
  puts(filepath);
#endif
  connection->fileFd = open(filepath, O_RDONLY);
  exitIfError(connection->fileFd, "Error opening file");
  sendHeaders(connection, 200);
  sendFile(connection);
  closeConnection(connection);
}

/**
 * Resolves a given port representation to a valid port number.
 *
 * \param service Name of the service (e.g. http) or the port number.
 * \returns The resolved port number or -1 in case of errors.
 */
int resolvePort(char * service)
{
/*see if service is already a port number*/
  int port = strtoul(service, NULL, 0);
  if (port>0)
  {
    /* valid number given */
    if (port<65536)
      return htons(port);
    fprintf(stderr, "Given port %d is out of valid port range!\n", port);
    return -1;
  }
#ifdef DEBUG
  printf("Port resolution requested for port \"%s\"\n", service);
#endif
  struct servent * service_struct = getservbyname(service, "tcp");
  if (service_struct == 0)
  {
    fputs("Port could not be resolved!\n", stderr);
    return -1;
  }
  port = service_struct->s_port;
#ifdef DEBUG
  printf("Resolved port: %d\n", ntohs(port));
#endif
  return port;
}

/**
 * Starts a server listing on a specified port
 * \param port_s The Port or service name to listen on
 */
void server(char * port_s)
{
  int port = resolvePort(port_s);
  if (port == -1)
    exit(1);

  /* create socket */
  listeningSocket = socket(AF_INET,SOCK_STREAM, 0);
  exitIfError(listeningSocket, "Error creating socket");

  /* stop socket from blocking the port after disconnecting */
  int sockopt = 1;
  int result = setsockopt(listeningSocket, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof(sockopt));
  exitIfError(result, "Error setting socket options");

  /* bind to port */
  struct sockaddr_in localAddr, remoteAddr;
  localAddr.sin_family = AF_INET;
  localAddr.sin_port = port;
  /* on all interfaces */
  localAddr.sin_addr.s_addr = INADDR_ANY;
  result = bind(listeningSocket, (struct sockaddr*)&localAddr, sizeof(localAddr));
  exitIfError(result, "Error binding to port");
  
  /* start listening */
  result = listen(listeningSocket, 1); /* only one client allowed */
  exitIfError(result, "Error listening");

  /* accept connections */
  socklen_t remoteAddrLength = sizeof(remoteAddr);
  int communicationSocket = accept(listeningSocket, (struct sockaddr*) &remoteAddr, &remoteAddrLength); 
  if (communicationSocket == -1)
    perror("Error accepting connection");
  else
  {
    /* replace listening socket with communicationSocket, so there is only one socket to close on catching signals etc. */
    close(listeningSocket);
    listeningSocket = -1;
    /* initialize new connection */
    connections[0].fileFd = -1;
    connections[0].status = statusOpen;
    connections[0].socketFd = communicationSocket;
  }
  processRequest(&connections[0]);
}

/**
 * Callback to handle signals.
 * \param signal The signal number received.
 */
void signalHandler(int signal)
{
  if (SIGTERM == signal || SIGINT == signal)
  {
    #ifdef DEBUG
      puts("Caught Signal SIGTERM or SIGINT, exiting...\n");
    #endif
    exit(0);
  }
}

/**
 * Parse the given command line arguments and act accordingly.
 * \param argc The argument count
 * \param argv The command line arguments
 */
void parseCmdLineArguments(int argc, char* argv[])
{
  static struct option long_options[] =
  {
    {"help", no_argument, 0, 'h'},
    /*{"listen", no_argument, 0, 'l'},*/
    {"port", required_argument, 0, 'p'},
    {0,0,0,0} /* end-of-array-marker */
  };

  /*parse options*/
  int port = 0;
  char port_s[21];
  memset(port_s, 0, sizeof(port_s));
  for (;;)
  {
    int result = getopt_long(argc, argv, "hp:", (struct option *)&long_options, NULL);

    if (result == -1)
      break;
    switch(result)
    {
      case 'h':
        puts("HTTPD: A web server by Sebastian Dörner");
        puts("start server:\t nc [-p port]");
        puts("options:");
        puts("\t-p port\t\t port to listen on (Default: 80)");
        exit(0);
        break;
      case 'p':
      #ifdef DEBUG
        printf("Option PORT with value %s\n", optarg);
        printf("Size of optarg is %d\n", strlen(optarg));
      #endif
        if (strlen(optarg)>20)
          fputs("Warning: length of the PORT argument should be no longer than 20 characters, stripping the rest...\n", stderr);
        strncpy(port_s, optarg,20);
        port_s[20] = '\0';
        port = atoi(optarg);
        break;
      case ':': 
      #ifdef DEBUG
        puts("Missing parameter\n");
      #endif
        break;
      case '?': 
      #ifdef DEBUG
        puts("Unknown option\n");
      #endif
        break;
    }
  }
  #ifdef DEBUG
    puts("");
  #endif
  
  /*react to given options*/
  if (port_s[0] =='\0')
  {
    fputs("ERROR: No port given!\n", stderr);
    exit(1);
  }
  server(port_s);
}

/**
 * The main function of the program.
 * \param argc The argument count
 * \param argv The command line arguments
 */
int main (int argc, char * argv[])
{
  memset(connections, 0, sizeof(connections));
  /*register signal handlers*/
  signal( SIGTERM, signalHandler);
  signal( SIGINT, signalHandler);
  /*register cleanUp function*/
  int result = atexit(cleanUpOnExit);
  exitIfError(result, "Error registering exit function:");
  parseCmdLineArguments(argc, argv);
  return 0;
}
