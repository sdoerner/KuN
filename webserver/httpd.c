/**
 * \file httpd.c
 * \brief A basic web server
 */

#include "util.h"
#include "log.h"

/*#define NDEBUG*/

#include <assert.h>
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
/** \brief Maximum size of requestable urls */
#define MAX_URL_SIZE 256
/** \brief Maximum size of the absolute path of any file to be delivered */
#define MAX_FILE_PATH_SIZE MAX_URL_SIZE + 29
/** \brief Document root of the web server (where the web files are located) */
const char documentRoot[] = "/home/sdoerner/svn/KuN/htdocs";
/** \brief Set if we want to enable debug output. */
#define DEBUG
/** \brief Number of file descriptors to check when calling poll */
#define FDCOUNT 2
/** \brief Maximal number of active connections */
#define MAXCON 10

/** \brief The number of slots we overallocate when rebuilding the poll struct */
#define INITIAL_FREE_SLOTS_IN_POLLSTRUCT 8

/** \brief The access log file */
#define ACCESSLOG "./logs/access.log"
/** \brief The error log file */
#define ERRORLOG "./logs/error.log"

/** \brief The status of a connection */
typedef enum
{
  statusClosed,
  statusIncomingRequest,
  statusOutgoingAnswer,
} statusType;

/** \brief All relevant information about an active connection */
struct connectionType
{
  /** \brief Status of the connection */
  statusType status;
  /** \brief File descriptor for the requested file */
  int fileFd;
  /** \brief File descriptor for the network socket */
  int socketFd;
  /** \brief First index that has not been written or sent yet */
  int bufferFreeOffset;
  /** \brief Actual size of sensible content in the buffer */
  int bufferLength;
  /** \brief The previous connection in our list */
  struct connectionType * prev;
  /** \brief The next connection in our list */
  struct connectionType * next;
  /** \brief Index of the corresponding entry in the \a pollStruct array */
  int pollStructIndex;
  /** \brief Buffer for information received or to be sent*/
  char buffer[BUFFER_SIZE];
};

/** \brief The only open socket at any time (almost). */
int listeningSocket = -1;

/**
 * \brief First element of the list of active connections
 */
struct connectionType * connectionHead = 0;
/**
 * \brief Last element of the list of active connections
 */
struct connectionType * connectionTail = 0;
/**
 * \brief Poll struct array
 *
 * At any time the first part is full, the rest is null.
 */
struct pollfd * pollStruct;
/** \brief Size of the \a pollStruct array */
int pollStructSize;
/** \brief First free index in \a pollStruct that can be filled by newly accepted connections. */
int nextFreePollStructIndex = 1;

/** \brief The server's access log */
struct log * accessLog = 0;
/** \brief The server's error log */
struct log * errorLog = 0;

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
  struct connectionType * conIt = connectionHead;
  while (conIt != 0)
  {
    free(conIt->prev); /* free(0) does nothing */
    assert(conIt->status != statusClosed); /* closed connections are not in our list */
    close (conIt->socketFd);
    if (conIt->fileFd!=-1)
      close(conIt->fileFd);
    conIt = conIt->next;
  }
  free(connectionTail);
  free(pollStruct);
  freeLog(accessLog);
  freeLog(errorLog);
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
 * Resizes the poll struct
 */
void resizePollStruct()
{
#ifdef DEBUG
  puts("Resizing poll struct");
#endif
  /* nextFreePollStructIndex - 1 = # active connections */
  /* 3 = listening socket + 0-Vector + new overflow connection that caused the rebuild */
  int newPollStructSize = nextFreePollStructIndex - 1 + 3 + INITIAL_FREE_SLOTS_IN_POLLSTRUCT;
  struct pollfd * newStruct = realloc(pollStruct, newPollStructSize * sizeof(struct pollfd));
  if (newStruct == 0)
  {
    fputs("Could not allocate new space for pollstruct", stderr);
    exit(1);
  }
  /* null the newly allocated space */
  memset(newStruct + pollStructSize, 0, sizeof(struct pollfd) * (newPollStructSize - pollStructSize));
  pollStruct = newStruct;
  pollStructSize = newPollStructSize;
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

/**
 * Stores the headers for the given \a statusCode in the buffer
 * \param connection Connection in whose buffer the headers are stored.
 * \param statusCode HTTP status code that determines the headers.
 */
void bufferHeaders(struct connectionType * connection, int statusCode)
{
int offset;
  switch (statusCode)
  {
    case 200:
    {
      const char statusCodeString[] = "HTTP/1.0 200 OK\r\n";
      time_t currentSeconds = time (NULL);
      struct tm * currentGMT = gmtime(&currentSeconds);
      char dateMessage[40];
      if (strftime(dateMessage, 40, "Date: %a, %d %b %Y %H:%M:%S %Z\r\n", currentGMT)==0)
      {
        fputs("Error creating dateMessage", stderr);
        exit(1);
      }

      if (strlen(dateMessage) + strlen(statusCodeString) + 3 > BUFFER_SIZE)
      {
        fputs("Error: Buffer too small for HTTP answer 200", stderr);
        exit(1);
      }
      strcpy(connection->buffer, statusCodeString);
      offset = strlen(statusCodeString);
      strcpy(connection->buffer + offset, dateMessage);
      offset = strlen(connection->buffer);
      break;
    }
    case 404:
    {
    #ifdef DEBUG
    puts("Buffering 404 headers");
    #endif
      const char statusCodeString[] = "HTTP/1.0 404 Not Found\r\n";
      if (strlen(statusCodeString) + 3 > BUFFER_SIZE)
      {
        fputs("Error: Buffer too small for HTTP answer 404", stderr);
        exit(1);
      }
      strcpy(connection->buffer, statusCodeString);
      offset = strlen(connection->buffer);
      break;
    }
    default:
       return;
  }
  strcpy(connection->buffer + offset, "\r\n");
  connection->bufferLength = strlen(connection->buffer);
  connection->bufferFreeOffset = 0;
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
#ifdef DEBUG
  puts("Closing connection");
#endif
  /* detach from list */
  if (connection->prev == 0)
  {
    assert(connectionHead == connection);
    connectionHead = connection->next;
  }
  else
    connection->prev->next = connection->next;

  if (connection->next == 0)
  {
    assert(connectionTail == connection);
    connectionTail = connection->prev;
  }
  else
    connection->next->prev = connection->prev;

  /* close fds */
  if (close(connection->socketFd) == -1)
    fputs("Error closing socket", stderr);
  connection->socketFd = -1;
  if (connection->fileFd!=-1 && close(connection->fileFd) == -1)
    fputs("Error closing file", stderr);

  /* swap last poll entry to this position */
  if (connection->pollStructIndex != nextFreePollStructIndex-1)
  {
    /* TODO dammit we get O(n) time here */
    /* find last entry in poll struct */
    struct connectionType * conIt = connectionTail;
    while (conIt != 0)
    {
      if (conIt->pollStructIndex == nextFreePollStructIndex-1)
        break;
      conIt = conIt->prev;
    }
    assert(conIt->pollStructIndex == nextFreePollStructIndex-1);
    /* copy it to our position */
    memcpy(pollStruct + connection->pollStructIndex,
           pollStruct + conIt->pollStructIndex,
           sizeof(struct pollfd));
    /* adapt connection struct */
    conIt->pollStructIndex = connection->pollStructIndex;
  }
  /* clean the old position */
  --nextFreePollStructIndex;
  memset(pollStruct + nextFreePollStructIndex, 0, sizeof(struct pollfd));
  /* TODO downsize poll struct if necessary */
  free(connection);
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
    /*puts(tokenStart);*/
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
 * Read from a given connection and initialize resulting actions.
 * \param connection The connection to read from
 */
void receiveConnection(struct connectionType * const connection)
{
  int length = receiveMessage(connection->socketFd, connection->buffer + connection->bufferFreeOffset, BUFFER_SIZE - connection->bufferFreeOffset);
  if (length == 0)
  {
    fprintf(stderr, "Error: Connection closed by client");
    exit(1);
  }
  else
  {
    connection->bufferFreeOffset += length;
    /* TODO dynamic buffer size */
    connection->buffer[connection->bufferFreeOffset]='\0';
    if (0!=strstr(connection->buffer, "\r\n\r\n"))
    {
      /* prepare connection for sending */
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
      if (connection->fileFd == -1)
      {
        doLog(errorLog, "GET %s 404 Not Found", url);
        bufferHeaders(connection, 404);
        connection->fileFd = open("./error_documents/404.html", O_RDONLY);
      }
      else
      {
        doLog(accessLog, "GET %s 200 OK", url);
        bufferHeaders(connection, 200);
      }
      connection->status = statusOutgoingAnswer;
      pollStruct[connection->pollStructIndex].events = POLLOUT;
    }
  }
}

/**
 * Send the content of a buffer through the network.
 * \param connection The connection whose buffer and network
 * socket are to be used.
 */
void sendBuffer(struct connectionType * const connection)
{
  const char * toSend = connection->buffer + connection->bufferFreeOffset;
  int len = connection->bufferLength - connection->bufferFreeOffset;
  int sent = write(connection->socketFd, toSend, len);
  exitIfError(sent, "Error writing to socket");
  if (sent == 0)
  {
    if (len == 0)
      fputs("Error: Send buffer was empty", stderr);
    else
      fputs("Error: Nothing was sent", stderr);
    exit(1);
  }
  connection->bufferFreeOffset+=sent;
}

/**
 * Sends the next piece of information over the network
 * \param connection The connection over which the information is to be sent
 */
void sendConnection(struct connectionType * const connection)
{
  /*
   * expect that there is something in the buffer to send
   * either filled by bufferHeaders or by last call to sendConnection
   */
  assert(connection->bufferFreeOffset < connection->bufferLength);
  sendBuffer(connection);
  if (connection->bufferFreeOffset == connection->bufferLength)
  {
    if (connection->fileFd == -1)
      closeConnection(connection);
    else
    {
      /* fill buffer from file */
      int len = read(connection->fileFd, connection->buffer, BUFFER_SIZE-1);
      exitIfError(len,"Error reading from file");
      if (len > 0)
      {
        connection->bufferFreeOffset = 0;
        connection->bufferLength = len;
      }
      else /* eof */
        closeConnection(connection);
    }
  }
}

/**
 * Accepts a new client on the \a listeningSocket and inserts the new connection into all relevant data structures
 */
void acceptNewConnection()
{
  #ifdef DEBUG
  puts("Accepting new connection");
  fflush(stdout);
  #endif
  /* accept connections */
  struct sockaddr_in remoteAddr;
  socklen_t remoteAddrLength = sizeof(remoteAddr);
  int communicationSocket = accept(listeningSocket, (struct sockaddr*) &remoteAddr, &remoteAddrLength);
  if (communicationSocket == -1)
    perror("Error accepting connection");
  else
  {
    /* initialize new connection */
    struct connectionType * newConnection = malloc(sizeof(struct connectionType));
    memset(newConnection, 0, sizeof(struct connectionType));
    newConnection->status = statusIncomingRequest;
    newConnection->fileFd = -1;
    newConnection->socketFd = communicationSocket;

    /* initialize poll struct */
    if (nextFreePollStructIndex>=pollStructSize-1) /* no space left */
    resizePollStruct();

    /* claim the next slot */
    newConnection->pollStructIndex = nextFreePollStructIndex;
    pollStruct[nextFreePollStructIndex].fd = communicationSocket;
    pollStruct[nextFreePollStructIndex].events = POLLIN;
    #ifdef DEBUG
    printf("new revents: %d\n", pollStruct[nextFreePollStructIndex].revents);
    #endif
    ++nextFreePollStructIndex;

    /* insert into connection list */
    if (connectionTail == 0) /* no connection yet */
      connectionTail = connectionHead = newConnection;
    else
    {
      /* put it at the end of the list */
      newConnection->prev = connectionTail;
      connectionTail->next = newConnection;
      connectionTail = newConnection;
    }
  }
}

/**
 * Main Loop: Handle all incoming traffic
 */
void talkToClients()
{
  int result;
  for (;;)
  {
    #ifdef DEBUG
    puts("new poll run");
    #endif
    result = poll(pollStruct, pollStructSize, -1);
    exitIfError(result, "Error on polling");
    if (result > 0)
    {
      #ifdef DEBUG
      puts("result > 0");
      fflush(stdout);
      #endif
      if (pollStruct[0].revents & POLLIN)
      {
        /* new caller on the listening socket */
        acceptNewConnection();
      }
      struct connectionType * conIt = connectionHead;
      struct connectionType * next;
      while (conIt != 0)
      {
        #ifdef DEBUG
        puts("itRun");
        #endif
        /* no need to check conIt->status because it corresponds to the active pollevents, which are a superset of the poll-r-events*/
        if (pollStruct[conIt->pollStructIndex].revents & (POLLHUP | POLLERR | POLLNVAL))
        {
          fputs("Error: Received POLLHUP/POLLERR/POLLNVAL",stderr);
          exit(1);
        }
        if (pollStruct[conIt->pollStructIndex].revents & POLLIN)
        {
          #ifdef DEBUG
          puts("POLLIN");
          #endif
          receiveConnection(conIt);
        }
        /* sendConnection might dispose the struct */
        next = conIt->next;
        if (pollStruct[conIt->pollStructIndex].revents & POLLOUT)
        {
          #ifdef DEBUG
          puts("POLLOUT");
          #endif
          sendConnection(conIt);
        }
        conIt = next;
      }
    }
    #ifdef DEBUG
    else
    {
      puts("result == 0");
      fflush(stdout);
    }
    #endif
  }
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
  struct sockaddr_in localAddr;
  localAddr.sin_family = AF_INET;
  localAddr.sin_port = port;
  /* on all interfaces */
  localAddr.sin_addr.s_addr = INADDR_ANY;
  result = bind(listeningSocket, (struct sockaddr*)&localAddr, sizeof(localAddr));
  exitIfError(result, "Error binding to port");

  /* start listening */
  result = listen(listeningSocket, 1); /* only one client allowed */
  exitIfError(result, "Error listening");
  #ifdef DEBUG
  puts("Server started, talking to clients");
  #endif
  /* init poll struct */
  pollStructSize = 1 + INITIAL_FREE_SLOTS_IN_POLLSTRUCT;
  pollStruct = calloc(pollStructSize, sizeof(struct pollfd));
  pollStruct[0].fd = listeningSocket;
  pollStruct[0].events = POLLIN;
  /* init logs */
  accessLog = initLog(ACCESSLOG);
  errorLog = initLog(ERRORLOG);
  if (accessLog == NULL || errorLog == NULL)
  {
    fputs("Logs are not accessible!\n", stderr);
    exit(1);
  }

  talkToClients();
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
  /*register signal handlers*/
  signal( SIGTERM, signalHandler);
  signal( SIGINT, signalHandler);
  /*register cleanUp function*/
  int result = atexit(cleanUpOnExit);
  exitIfError(result, "Error registering exit function:");
  parseCmdLineArguments(argc, argv);
  return 0;
}
