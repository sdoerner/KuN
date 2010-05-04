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
#include <unistd.h> //getopt
#include <getopt.h>

///The host to connect to (temporarily hardcoded for now)
#define HOST "127.0.0.1"
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
 * Starts normal communication through global socket \a sock.
 */
void communicate()
{
  char buffer[BUFFER_SIZE];
  for (;;)
    send_message(sock, buffer);
}

/**
 * Starts a server listing on a specified port
 * \param port The Port to listen on
 */
void server(int port)
{
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
  localAddr.sin_port = htons(port);
  //on all interfaces
  localAddr.sin_addr.s_addr = INADDR_ANY;
  result = bind(sock, (struct sockaddr*)&localAddr, sizeof(localAddr));
  exitIfError(result, "Error binding to port");
  
  //start listening
  result = listen(sock, 1); // only one client allowed

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
 * Send a string message through a socket.
 * \param sock Socket descriptor for the socket to send the message through.
 * \param buffer The message to send
 */
int send_message(int sock, char* buffer)
{
  int len = read(0,buffer,BUFFER_SIZE);
  exitIfError(len,"Error reading from console");
  len = write(sock, buffer, len);
  exitIfError(len, "Error writing to socket");
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
 * \param port The port to connect to.
 */
void client(int port)
{
  //create socket
  sock = socket(PF_INET, SOCK_STREAM, 0);
  exitIfError(sock,"Error creating socket");

  //TODO: 
  //DNS-Auflösung: man getaddrinfo
  //strct addrinfo hints, *res, *rp;
  //rest auf null:
  //memset(&hints, 0, sizeof(hints))
  //hints.ai_family = AF_INET;
  //hints.ai_socktype = SOCK_STREAM;
  //getaddrinfo("euklab-117", "5555", &hints, &res);
  //res enthaelt mehrere resultate, alle durchgehen
  //PSEUDOCODE
  //rp = res;
  //while (socket( rp->ai_familily, rp->ai_socktype, 0))
  //  fail ==> rp = rp->ai_next;
  //  res = connect(sock, rp->ai_addr, rp->ai_addrlen)
  //  if (res == -1)
  //  close(sock)
  //
  //freeaddrinfo(res)

  //connect to target
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_aton(HOST,&addr.sin_addr);
  int result = connect(sock, (struct sockaddr *) &addr, sizeof(addr));
  exitIfError(result,"Error connecting to socket");

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
  for (;;)
  {
    int result = getopt_long(argc, argv, "lp:", (struct option *)&long_options, NULL);

    if (result == -1)
      break;
    switch(result)
    {
      case 'l':
      #ifdef DEBUG
        puts("Option LISTEN\n");
        listen = 1;
      #endif
        break;
      case 'p':
      #ifdef DEBUG
        printf("Option PORT with value %s\n", optarg);
        port = atoi(optarg);
      #endif
        break;
      case ':': 
      #ifdef DEBUG
        puts("Missing Parameter\n");
      #endif
        break;
      case '?': 
      #ifdef DEBUG
        puts("Unbekannte Option\n");
      #endif
        break;
    }
  }
  //react to given options
  
  if (!port)
  {
    puts("ERROR: No port given!");
    exit(1);
  }

  if (listen)
  {
    server(port);
  }
  else
  {
    #ifdef DEBUG
      puts("Starting client");
    #endif
    client(port);
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
