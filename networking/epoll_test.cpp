//
// Created by jc on 26/10/23.
//

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/epoll.h>

#define MAX_EVENTS 10

inline int set_nonblocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL);
    if (flags == -1) {
        perror("fcntl");
        return -1;
    }
    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl");
        return -1;
    }
    return 0;
}

inline int epoll_test() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Set socket options and bind
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8889);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 5) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    // Set server socket to non-blocking
    set_nonblocking(server_fd);

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    struct epoll_event event, events[MAX_EVENTS];
    event.events = EPOLLIN;
    event.data.fd = server_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event)) {
        perror("epoll_ctl");
        exit(EXIT_FAILURE);
    }

    while (true) {
        int num_fds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < num_fds; i++) {
            if (events[i].data.fd == server_fd) {
                new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
                if (new_socket < 0) {
                    perror("accept");
                    exit(EXIT_FAILURE);
                }
                set_nonblocking(new_socket);
                event.data.fd = new_socket;
                event.events = EPOLLIN;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_socket, &event);
            } else {
                char buffer[1024] = {0};
                int valread = read(events[i].data.fd, buffer, sizeof(buffer));
                if (valread <= 0) {
                    close(events[i].data.fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
                } else {
//                    send(events[i].data.fd, buffer, valread, 0);
                }
            }
        }
    }

    return 0;
}

