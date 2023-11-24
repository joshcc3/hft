#include <linux/if_ether.h>

#include <cstdio>
#include <cstdint>
#include <cassert>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

static void hex_dump(u8 *pkt, size_t length, u64 addr) {
    const unsigned char *address = (unsigned char *)pkt;
    const unsigned char *line = address;
    size_t line_size = 32;
    unsigned char c;
    char buf[32];
    int i = 0;

    sprintf(buf, "addr=%lu", addr);
    printf("length = %zu\n", length);
    printf("%s | ", buf);
    while (length-- > 0) {
        printf("%02X ", *address++);
        if (!(++i % line_size) || (length == 0 && i % line_size)) {
            if (length == 0) {
                while (i++ % line_size)
                    printf("__ ");
            }
            printf(" | ");	/* right close */
            while (line < address) {
                c = *line++;
                printf("%c", (c < 33 || c == 255) ? 0x2E : c);
            }
            printf("\n");
            if (length > 0)
                printf("%s | ", buf);
        }
    }
    printf("\n");
}



static inline unsigned short from32to16(unsigned int x)
{
	/* add up 16-bit and 16-bit for 16+c bit */
	x = (x & 0xffff) + (x >> 16);
	/* add up carry.. */
	x = (x & 0xffff) + (x >> 16);
	return x;
}

static unsigned int do_csum(const unsigned char *buff, int len)
{
	unsigned int result = 0;

	assert(len == 20);
	assert(((unsigned long)buff & 0xf) == 0xe);

	if (2 & (unsigned long) buff) {
		result += *(unsigned short *) buff;
		len -= 2;
		buff += 2;
	}
	assert(len >= 4);

	const unsigned char *end = buff + ((unsigned)len & ~3);
	unsigned int carry = 0;

	do {
		unsigned int w = *(unsigned int *) buff;
		buff += 4;
		result += carry;
		result += w;
		carry = (w > result);
	} while (buff < end);

	result += carry;
	result = (result & 0xffff) + (result >> 16);

	if (len & 2) {
		result += *(unsigned short *) buff;
		buff += 2;
	}

	result = from32to16(result);

	return result;
}


u16 ip_fast_csum(const u8 *iph, u32 ihl) {
	return (u16)~do_csum(iph, ihl*4);
}
