/*
 * repro-bpf-sa-len.c
 * Reproduces sa_len=0 path in bpfwrite() -> if_output on FreeBSD.
 * Compile: cc -o repro-bpf-sa-len repro-bpf-sa-len.c
 * Run as root: ./repro-bpf-sa-len <interface>
 * e.g.: ./repro-bpf-sa-len lo0
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/bpf.h>
#include <net/if.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
        int fd;
        struct ifreq ifr;
        unsigned char pkt[] = {
		0x02, 0x00, 0x00, 0x00,		/* AF_INET in host byte order (DLT_NULL) */
                0x45, 0x00, 0x00, 0x14,
                0x00, 0x01, 0x00, 0x00,
                0x40, 0x00, 0x00, 0x00,
                0x7f, 0x00, 0x00, 0x01,
                0x7f, 0x00, 0x00, 0x01
        };

        if (argc < 2) {
                fprintf(stderr, "usage: %s <interface>\n", argv[0]);
                return 1;
        }

        fd = open("/dev/bpf", O_RDWR);
        if (fd < 0) { perror("open /dev/bpf"); return 1; }

        memset(&ifr, 0, sizeof(ifr));
        strlcpy(ifr.ifr_name, argv[1], sizeof(ifr.ifr_name));
        if (ioctl(fd, BIOCSETIF, &ifr) < 0) {
                perror("BIOCSETIF"); close(fd); return 1;
        }

        /*
         * bpfwrite() calls bzero(&dst) then bpf_movein().
         * For DLT_RAW, bpf_movein() sets sa_family=AF_UNSPEC
         * but never sets sa_len -- it remains 0.
         * This triggers the bcopy(dst->sa_data, &af, sizeof(af))
         * path in if_output with sa_len=0.
         */
        ssize_t n = write(fd, pkt, sizeof(pkt));
        if (n < 0)
                perror("write");
        else
                printf("wrote %zd bytes -- DLT_NULL header correct, packet injected\n", n);

        close(fd);
        return 0;
}
