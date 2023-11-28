from struct import unpack, pack

def udp_checksum(packet):
    # Packet needs to be a multiple of 2 bytes for checksum calculation
    if len(packet) % 2 != 0:
        packet += b'\x00'

    # Sum all 16-bit words
    checksum = sum(unpack('!{}H'.format(len(packet) // 2), packet))

    # Add overflow bits
    while checksum >> 16:
        checksum = (checksum & 0xffff) + (checksum >> 16)

    # One's complement
    checksum = ~checksum & 0xffff
    return checksum

# Extracted data from the packet
source_ip = b'\x7f\x00\x00\x01'  # 127.0.0.1
dest_ip = b'\x7f\x00\x00\x01'    # 127.0.0.1
reserved = 0
protocol = 17  # UDP
udp_length = 27

# UDP Header and Data (from 20th byte to the end)
udp_packet = bytes.fromhex("04d2 10e1 001b 6b1d 0200 0000 0000 0100 0000 0100 0000 6200 0000 00")

# Pseudo header for checksum calculation
pseudo_header = source_ip + dest_ip + pack('!BBH', reserved, protocol, udp_length)

# Checksum calculation over pseudo header + UDP packet
complete_packet = pseudo_header + udp_packet
computed_checksum = udp_checksum(complete_packet)
computed_checksum_hex = format(computed_checksum, '04x')

if __name__ == '__main__':
    print(computed_checksum_hex)