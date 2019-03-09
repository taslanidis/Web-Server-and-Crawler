#include <sys/socket.h>                                                         /* sockets */
#include <sys/types.h>                                                          /* sockets */
#include <sys/wait.h>                                                           /* sockets */
#include <unistd.h>                                                             /* fork */
#include <netdb.h>                                                              /* gethostbyaddr */
#include <stdint.h>                                                             /* in_port_t, in_address_t */
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>                                                            /* threads */


/*int bind_on_port(int sock, short port){
    sockaddr_in server;
    server.sin_family = AF_INET;
    (server.sin_addt).sin_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port);
    return bind(sock, (struct sockaddr *) &server, sizeof(server));
}*/
