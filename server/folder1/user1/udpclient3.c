#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERV_PORT 1255
#define MAXLINE 250

int main() {
    int sockfd, n;
    socklen_t addrlen;
    char sendline[MAXLINE], recvline[MAXLINE + 1];
    struct sockaddr_in servaddr, from_socket;

    addrlen = sizeof(from_socket);

    // Set up the server address structure
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERV_PORT);
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");  // Change to your server's IP address

    // Create the UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    printf("Socket created\n");

    while (fgets(sendline, MAXLINE, stdin) != NULL) {
        // Send the message to the server
        printf("To Server: %s", sendline);
        sendto(sockfd, sendline, strlen(sendline), 0, (struct sockaddr *) &servaddr, sizeof(servaddr));

        // Receive the response from the server (which is from the other client)
        n = recvfrom(sockfd, recvline, MAXLINE, 0, (struct sockaddr *) &from_socket, &addrlen);
        recvline[n] = 0;  // Null-terminate the received message

        // Print the message received from the other client (via the server)
        printf("Receive from %s:%d %s\n", inet_ntoa(from_socket.sin_addr), ntohs(from_socket.sin_port), recvline);
    }

    close(sockfd);
    return 0;
}
