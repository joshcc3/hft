import socket

# Create a UDP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# Bind the socket to the address and port
server_address = ('localhost', 4321)
sock.bind(server_address)

print("Listening for UDP packets on localhost:4321...")

# Loop indefinitely to listen for incoming datagrams
while True:
    data, address = sock.recvfrom(4096)
    print(f"Received {len(data)} bytes from {address}: {data.decode('utf-8')}")
