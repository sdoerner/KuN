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

/// Size of input buffers
#define BUFFER_SIZE 64
///Set if we want to enable debug output.
#define DEBUG

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
 * \param buffer The message to send
 */
void send_message(int sock, char* buffer)
{
  int len = read(0,buffer,BUFFER_SIZE);
  exitIfError(len,"Error reading from console");
  len = write(sock, buffer, len);
  exitIfError(len, "Error writing to socket");
}

/**
 * Starts normal communication through global socket \a sock.
 */
void communicate()
{
  char buffer[BUFFER_SIZE];
  for (;;)
    send_message(sock, buffer);
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
    printf("Given port %d is out of valid port range!\n", port);
    return -1;
  }
#ifdef DEBUG
  printf("Port resolution requested for port \"%s\"\n", service);
#endif
  struct servent * service_struct = getservbyname(service, "tcp");
  if (service_struct == 0)
  {
    puts("Port could not be resolved!");
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
  socklen_t remoteAddrLength;
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
    puts("Caught Signal SIGTERM or SIGINT, exiting...\n");
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
    printf("Error resolving address \"%s\". Exiting.\n", host);
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
    int result = getopt_long(argc, argv, "lp:", (struct option *)&long_options, NULL);

    if (result == -1)
      break;
    switch(result)
    {
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
          puts("Warning: length of the PORT argument should be no longer than 20 characters, stripping the rest...\n");
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
  //react to given options
  puts("");
  
  if (port_s[0] =='\0')
  {
    puts("ERROR: No port given!");
    exit(1);
  }
    printf("%d\n", optind);

  if (listen)
  {
    server(port_s);
  }
  else
  {
    //optind is index of the first argument that is no option
    if (argc<=optind)
    {
      puts("No destination");
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
  printf("%d\n", argc);
  //register signal handlers
  signal( SIGTERM, signalHandler);
  signal( SIGINT, signalHandler);
  //register cleanUp function
  int result = atexit(cleanUpOnExit);
  exitIfError(result, "Error registering exit function:");
  parseCmdLineArguments(argc, argv);
}
