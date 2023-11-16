import socket

def send_udp_data(message, port):
    # Create a UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    # Define the loopback address and port
    server_address = ('127.0.0.1', port)

    try:
        # Send the message
        sent = sock.sendto(message.encode(), server_address)
        print(f"Sent {sent} bytes to {server_address}")
    finally:
        # Close the socket
        sock.close()

# Example usage
send_udp_data("Hello, UDP!", 12345)
