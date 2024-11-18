#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>

#define SERVER_IP "127.0.0.1"
#define PORT 8080

int main() {
    int sock;
    struct sockaddr_in server_addr;
    char message[1024], server_reply[1024];

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        printf("Could not create socket\n");
        return 1;
    }

    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    // Connect to server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        return 1;
    }

    printf("Connected to server\n");

    // Interact with server
    while (1) {
        printf("Enter command (REGISTER/LOGIN/LOGOUT): ");
        fgets(message, 1024, stdin);
        message[strcspn(message, "\n")] = '\0';  // Remove newline

        // Send message to server
        if (send(sock, message, strlen(message), 0) < 0) {
            printf("Send failed\n");
            break;
        }

        // Receive server response
        if (recv(sock, server_reply, 1024, 0) < 0) {
            printf("Receive failed\n");
            break;
        }

        printf("Server reply: %s\n", server_reply);
    }

    close(sock);
    return 0;
}
