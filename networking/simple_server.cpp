//
// Created by jc on 26/10/23.
//
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "errno.h"


inline int simpleServer() {
    int server_fd, client_fd, valread;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};
    char *hello = (char *) "Echo from server\0";

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        std::cerr << "Socket creation failed" << std::endl;
        exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to port 8888
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        std::cerr << "setsockopt failed" << std::endl;
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8889);

    // Bind the socket to the network address and port

    std::string errors[255] = {};
    errors[EACCES] = "EACCES";
    errors[EADDRINUSE] = "EADDRINUSE";
    errors[EADDRINUSE] = "EADDRINUSE";
    errors[EBADF] = "EBADF";
    errors[EINVAL] = "EINVAL";
    errors[EINVAL] = "EINVAL";
    errors[ENOTSOCK] = "ENOTSOCK";
    errors[EACCES] = "EACCES";
    errors[EADDRNOTAVAIL] = "EADDRNOTAVAIL";
    errors[EFAULT] = "EFAULT";
    errors[ELOOP] = "ELOOP";
    errors[ENAMETOOLONG] = "ENAMETOOLONG";
    errors[ENOENT] = "ENOENT";
    errors[ENOMEM] = "ENOMEM";
    errors[ENOTDIR] = "ENOTDIR";
    errors[EROFS] = "EROFS";

    int errorCode;
    if ((errorCode = bind(server_fd, (struct sockaddr *) &address, sizeof(address))) < 0) {
        std::cerr << "Bind failed: " << errno << " " << errors[errno] << std::endl;
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 3) < 0) {
        std::cerr << "Listen failed" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout << "Server listening on port 8889" << std::endl;

    while (true) {
        if ((client_fd = accept(server_fd, (struct sockaddr *) &address, (socklen_t *) &addrlen)) < 0) {
            std::cerr << "Accept failed" << std::endl;
            exit(EXIT_FAILURE);
        }

        valread = read(client_fd, buffer, 1024);
//        send(client_fd, buffer, valread, 0);
        close(client_fd);
    }

    return 0;
}