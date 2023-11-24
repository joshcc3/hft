#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PORT 4321

int main() {
    int s;
    struct sockaddr_in saddr;
    unsigned char *buffer = (unsigned char *)malloc(65536); // Large buffer

    // Create raw socket
    s = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
    if(s < 0) {
        perror("Failed to create socket");
        exit(1);
    }

    while(1) {
        socklen_t saddr_size = sizeof(saddr);
        int data_size = recvfrom(s, buffer, 65536, 0, (struct sockaddr *)&saddr, &saddr_size);
        if(data_size < 0) {
            perror("recvfrom error");
            exit(1);
        }

        struct iphdr *ip_header = (struct iphdr *)buffer;
        struct udphdr *udp_header = (struct udphdr *)(buffer + ip_header->ihl * 4);

        if(ntohs(udp_header->dest) == PORT) {
            printf("Received UDP packet of size %d bytes\n", data_size);
        } else {
            printf("Received UDP packet on differe portn %d bytes\n", data_size);
	}
    }

    close(s);
    free(buffer);
    return 0;
}
