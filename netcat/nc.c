/**
 * \file nc.c
 * \brief The netcat implementation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/ip.h>
#include <netdb.h> //addrinfo
#include <unistd.h> //getopt
#include <getopt.h>
#include <poll.h>

/// Size of input buffers
#define BUFFER_SIZE 64
///Set if we want to enable debug output.
/*#define DEBUG*/
///Number of file descriptors to check when calling poll
#define FDCOUNT 2

// fd 0 -> eingabe, 1-> ausgabe, 2 -> fehlerausgabe
// man: ip, tcp, socket, bind, getopt, getopt_long

///The only open socket at any time (almost).
int sock = -1;

/**
 * Frees allocated ressources on exiting the program.
 * Is to be registered as a callback using atexit.
 */
void cleanUpOnExit()
{
  //try to close the socket if necessary
  if (sock != -1)
  {
  #ifdef DEBUG
    puts("Closing Socket on Exit.");
  #endif
    int result = close(sock);
    if (result == -1)
      perror("Error closing Socket");
    fflush(stdout);
  }
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
 * Send a string message through a socket.
 * \param sock Socket descriptor for the socket to send the message through.
 * \param buffer Buffer for buffering the message we send.
 */
void sendMessage(int sock, char* buffer)
{
  int len = read(0,buffer,BUFFER_SIZE);
  exitIfError(len,"Error reading from console");
  len = write(sock, buffer, len);
  exitIfError(len, "Error writing to socket");
}

/**
 * Receive a string message through a socket.
 * \param sock Socket descriptor for the socket to receive the message through.
 * \param buffer Buffer for buffering the message we receive.
 */
int receiveMessage(int sock, char* buffer)
{
  int len = read(sock,buffer,BUFFER_SIZE);
  exitIfError(len,"Error reading from socket");
  if (len == 0)
    return -1;
  len = write(1, buffer, len);
  exitIfError(len, "Error writing to console");
  return 0;
}

/**
 * Starts normal communication through global socket \a sock.
 */
void communicate()
{
  char buffer[BUFFER_SIZE];
  struct pollfd fds[FDCOUNT];
  memset(fds, 0, sizeof(fds));
  fds[0].fd = 0;
  fds[1].fd = sock;
  fds[0].events=fds[1].events = POLLIN;
  int result;

  for (;;)
  {
    result = poll(fds,FDCOUNT,-1);
    exitIfError(result, "Error on polling");
    if (result>0)
    {
      if (fds[1].revents & POLLHUP)
        break;
      if (fds[0].revents & POLLIN)
        sendMessage(sock, buffer);
      if (fds[1].revents & POLLIN)
        if (-1 == receiveMessage(sock, buffer))
          break;
    }
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
//see if service is already a port number
  int port = strtoul(service, NULL, 0);
  if (port>0)
  {
    //valid number given
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

  //create socket
  sock = socket(AF_INET,SOCK_STREAM, 0);
  exitIfError(sock, "Error creating socket");

  //stop socket from blocking the port after disconnecting
  int sockopt = 1;
  int result = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof(sockopt));
  exitIfError(result, "Error setting socket options");

  //bind to port
  struct sockaddr_in localAddr, remoteAddr;
  localAddr.sin_family = AF_INET;
  localAddr.sin_port = port;
  //on all interfaces
  localAddr.sin_addr.s_addr = INADDR_ANY;
  result = bind(sock, (struct sockaddr*)&localAddr, sizeof(localAddr));
  exitIfError(result, "Error binding to port");
  
  //start listening
  result = listen(sock, 1); // only one client allowed
  exitIfError(result, "Error listening");

  //accept connections
  socklen_t remoteAddrLength = sizeof(remoteAddr);
  int communicationSocket = accept(sock, (struct sockaddr*) &remoteAddr, &remoteAddrLength); 
  exitIfError(communicationSocket, "Error accepting connection");
  //replace listening socket with communicationSocket, so there is only one socket to close on catching signals etc.
  close(sock);
  sock = communicationSocket;
  communicate();
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
 * Create a client and connect to localhost on the specified port.
 * \param host The host name or ip address to connect to.
 * \param port The port to connect to.
 */
void client(char * host, char* port)
{
  #ifdef DEBUG
    puts("Client start requested.");
  #endif
  //resolve domain and port name and connect to target
  struct addrinfo hints, *res, *rp;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (0 != getaddrinfo(host, port, &hints, &res))
  {
    fprintf(stderr, "Error resolving address \"%s\". Exiting.\n", host);
    exit(1);
  }

  //evaluate results
  for (rp = res; rp != NULL; rp = rp->ai_next)
  {
      sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      if (sock == -1)
          continue;

      if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0)
          break;                  /* Success */

      close(sock);
      sock = -1;
  }
  freeaddrinfo(res);
  exitIfError(sock,"Error connecting to socket");

  //send messages
  communicate();
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
    {"listen", no_argument, 0, 'l'},
    {"port", required_argument, 0, 'p'},
    {0,0,0,0} //end-of-array-marker
  };

  //parse options
  int listen = 0, port = 0;
  char port_s[21];
  memset(port_s, 0, sizeof(port_s));
  for (;;)
  {
    int result = getopt_long(argc, argv, "hlp:", (struct option *)&long_options, NULL);

    if (result == -1)
      break;
    switch(result)
    {
      case 'h':
        puts("Netcat program by Sebastian Dörner");
        puts("connect to somewhere:\t nc -p port hostname");
        puts("listen for inbound:\t nc -p port -l\n");
        puts("options:");
        puts("\t-l\t\t listen");
        puts("\t-p port\t\t port to listen on or to connect to");
        puts("\t\t\t may be a port number or service name (see /etc/services)");
        puts("\thostname\t may be an ip address or domain name");
        exit(0);
        break;
      case 'l':
      #ifdef DEBUG
        puts("Option LISTEN");
      #endif
        listen = 1;
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
  
  //react to given options
  if (port_s[0] =='\0')
  {
    fputs("ERROR: No port given!\n", stderr);
    exit(1);
  }

  if (listen)
  {
    server(port_s);
  }
  else
  {
    //optind is index of the first argument that is no option
    if (argc<=optind)
    {
      fputs("No destination\n", stderr);
      exit(1);
    }
    client(argv[optind], port_s);
  }
}

/**
 * The main function of the program.
 * \param argc The argument count
 * \param argv The command line arguments
 */
int main (int argc, char * argv[])
{
  //register signal handlers
  signal( SIGTERM, signalHandler);
  signal( SIGINT, signalHandler);
  //register cleanUp function
  int result = atexit(cleanUpOnExit);
  exitIfError(result, "Error registering exit function:");
  parseCmdLineArguments(argc, argv);
}
