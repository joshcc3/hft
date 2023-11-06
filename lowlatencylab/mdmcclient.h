////
//// Created by jc on 04/11/23.
////
//
//#ifndef TICTACTOE_MDMCCLIENT_H
//#define TICTACTOE_MDMCCLIENT_H
//
//
//#include <iostream>
//#include <liburing.h>
//#include <netinet/in.h>
//#include <arpa/inet.h>
//#include <fcntl.h>
//#include <unistd.h>
//#include <string.h>
//
//#define QUEUE_DEPTH 1
//#define BLOCK_SZ    1024
//
//int main() {
//    io_uring ring;
//    int ret, fd;
//    sockaddr_in addr;
//    ip_mreq mreq;
//    io_uring_sqe* sqe;
//    io_uring_cqe* cqe;
//    char buf[BLOCK_SZ];
//
//    // Initialize io_uring
//    ret = io_uring_queue_init(QUEUE_DEPTH, &ring, 0);
//    if (ret) {
//        std::cerr << "Unable to setup io_uring: " << strerror(-ret) << std::endl;
//        return 1;
//    }
//
//    // Create a UDP socket
//    fd = socket(AF_INET, SOCK_DGRAM, 0);
//    if (fd < 0) {
//        perror("socket");
//        return 1;
//    }
//
//    // Set up the multicast group address
//    memset(&addr, 0, sizeof(addr));
//    addr.sin_family = AF_INET;
//    addr.sin_addr.s_addr = htonl(INADDR_ANY);
//    addr.sin_port = htons(12345);
//
//    // Allow multiple sockets to use the same port number
//    u_int yes = 1;
//    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
//        perror("Reusing ADDR failed");
//        return 1;
//    }
//
//    // Bind the UDP socket to the port
//    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
//        perror("bind");
//        close(fd);
//        return 1;
//    }
//
//    // Join the multicast group
//    mreq.imr_multiaddr.s_addr = inet_addr("224.0.0.1");
//    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
//    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
//        perror("setsockopt mreq");
//        close(fd);
//        return 1;
//    }
//
//    // Set up the io_uring structure to read from the socket
//    sqe = io_uring_get_sqe(&ring);
//    if (!sqe) {
//        std::cerr << "Could not get SQE." << std::endl;
//        close(fd);
//        return 1;
//    }
//
//    io_uring_prep_recv(sqe, fd, buf, BLOCK_SZ, 0);
//    sqe->flags |= IOSQE_FIXED_FILE;
//
//    // Submit the read
//    ret = io_uring_submit(&ring);
//    if (ret < 0) {
//        std::cerr << "io_uring_submit: " << strerror(-ret) << std::endl;
//        close(fd);
//        return 1;
//    }
//
//    // Wait for completion
//    ret = io_uring_wait_cqe(&ring, &cqe);
//    if (ret < 0) {
//        std::cerr << "io_uring_wait_cqe: " << strerror(-ret) << std::endl;
//        close(fd);
//        return 1;
//    }
//
//    // Check the result
//    if (cqe->res < 0) {
//        std::cerr << "Async read failed: " << strerror(-cqe->res) << std::endl;
//        io_uring_cqe_seen(&ring, cqe);
//        close(fd);
//        return 1;
//    }
//
//    // Print the received message
//    std::cout << "Received " << cqe->res << " bytes." << std::endl;
//    std::cout.write(buf, cqe->res);
//    std::cout << std::endl;
//
//    // Mark this cqe as seen
//    io_uring_cqe_seen(&ring, cqe);
//    close(fd);
//
//    // Tear down io_uring
//    io_uring_queue_exit(&ring);
//
//    return 0;
//}
//
//
//#endif //TICTACTOE_MDMCCLIENT_H
