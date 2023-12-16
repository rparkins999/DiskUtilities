/* Copyright © 2023 Richard P. Parkins, M. A.
 *
 * Released under the GPL
 */

#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MINBLOCKSIZE 512
#define MAXBLOCKSIZE 4096 // largest block size currently used

char * filename; // global for error reporting
size_t blocksize; // actual block size to save argument passing

// Print a size in human-friendly form
char * human(unsigned long long size) {
    if (size <= 9999) {
        return "";
    }
    double sz = size;
    static const char * names[] = {
        "bytes",
        "Kibytes",
        "Mibytes",
        "Gibytes",
        "Tibytes",
        "Pibytes",
        "Xibytes",
        "Zibytes"
    };
    int i = 0;
    for ( ; (i < 8) && (sz > 9999); ++i) {
        sz = sz / 1024;
    }
    static char buff[2+4+1+7+1];
    if (sz > 99.9) {
        snprintf(buff, sizeof(buff), ", %1.0f %s", sz, names[i]);
    } else {
        snprintf(buff, sizeof(buff), ", %1.1f %s", sz, names[i]);
    }
    return buff;
}

int confirm() {
    if (!isatty(fileno(stdin))) {
        printf("\nYou can only do this from a terminal\n");
        exit(0);
    }
    char * lineptr = NULL;
    size_t n = 0;
    ssize_t res = getline(&lineptr, &n, stdin);
    if (res < 0) {
        printf("\nError reading standard input: %\\n", strerror(errno));
        exit(-1);
    }
    return *lineptr == 'Y';
}

// seek then read with some error reporting
void checkedread(off_t address, void * buf, size_t size) {
    int fd = open(filename, O_LARGEFILE|O_RDWR);
    if (fd < 0) {
        switch (errno) {
            case ENODEV:
            case ENXIO:
            case ENOMEDIUM:
                printf("No device connected at %s\n", filename);
                exit(-1);
            case ENOENT:
                printf("%s does not exist\n", filename);
                exit(-1);
            case EPERM:
            case EACCES:
                printf("You aren't allowed to open %s\n", filename);
                exit(-1);
            default:
                printf("Error opening %s: %s\n", filename, strerror(errno));
                exit(-1);
        }
    }
    off_t n = lseek(fd, address, SEEK_SET);
    if (n < 0) {
        printf("seek to address %ld on %s failed: %s\n",
            address, filename, strerror(errno));
        exit(-1);
    } else if (n != address) {
        printf("Seek to %ld on %s went to %ld instead\n",
                address, filename, n);
        exit(-1);
    }
    ssize_t nn = read(fd, buf, size);
    if (nn < 0) {
        printf("Reading %d bytes at offset %lu from %s failed: %s\n",
                size, address, filename, strerror(errno));
        exit(-1);
    } else if (nn != size) {
        printf("Reading %d bytes at offset %lu from %s read %d bytes instead\n",
                size, address, filename, nn);
        exit(-1);
    }
    if (fsync(fd) != 0) {
        printf("Error fsync'ing %s: %s\n", filename, strerror(errno));
        exit(-1);
    }
    if (close(fd) != 0) {
        printf("Error closing %s: %s\n", filename, strerror(errno));
        exit(-1);
    }
}

// seek then write with some error reporting
void checkedwrite(off_t address, void * buf, size_t size) {
    int fd = open(filename, O_LARGEFILE|O_RDWR);
    if (fd < 0) {
        switch (errno) {
            case ENODEV:
            case ENXIO:
            case ENOMEDIUM:
                printf("No device connected at %s\n", filename);
                exit(-1);
            case ENOENT:
                printf("%s does not exist\n", filename);
                exit(-1);
            case EPERM:
            case EACCES:
                printf("You aren't allowed to open %s\n", filename);
                exit(-1);
            default:
                printf("Error opening %s: %s\n", filename, strerror(errno));
                exit(-1);
        }
    }
    off_t n = lseek(fd, address, SEEK_SET);
    if (n < 0) {
        printf("seek to address %ld on %s failed: %s\n",
            address, filename, strerror(errno));
        exit(-1);
    } else if (n != address) {
        printf("Seek to %ld on %s went to %ld instead\n",
                address, filename, n);
        exit(-1);
    }
    ssize_t nn = write(fd, buf, size);
    if (nn < 0) {
        printf("Writing %d bytes at offset %ld to %s failed: %s\n",
                size, address, filename, strerror(errno));
        exit(-1);
    } else if (nn != size) {
        printf("Writing %d bytes at offset %ld to %s wrote %d bytes instead\n",
                size, address, filename, nn);
        exit(-1);
    }
    if (fsync(fd) != 0) {
        printf("Error fsync'ing %s: %s\n", filename, strerror(errno));
        exit(-1);
    }
    if (close(fd) != 0) {
        printf("Error closing %s: %s\n", filename, strerror(errno));
        exit(-1);
    }
}

void partitions(off_t base, int pcount, int psize) {
    printf("    %d partitions of size %d at %ld to %ld:\n",
           pcount, psize, base, base + pcount * (long)psize);
    printf("    (empty partitions omitted)\n");
    unsigned char buffer [MAXBLOCKSIZE];
    off_t currentblock = base;
    checkedread(currentblock, buffer, blocksize);
    off_t paddr = 0;
    for (int p = 0; p < pcount; ++p) {
        if (paddr >= blocksize) {
            currentblock += blocksize;
            paddr -= blocksize;
            checkedread(currentblock, buffer, blocksize);
        }
        off_t start = *(off_t *)(buffer + paddr + 32) * blocksize;
        off_t end = *(off_t *)(buffer + paddr + 40) * blocksize;
        if (start != end) {
            printf("        from %ld to %ld\n", start, end);
        }
        paddr += psize;
    }
}

void readbacktest(off_t address, off_t modulo, int i) {
    unsigned char prevdata[MAXBLOCKSIZE];
    unsigned char originalreaddata[MAXBLOCKSIZE];
    unsigned char writedata[MAXBLOCKSIZE];
    unsigned char readbackdata[MAXBLOCKSIZE];
    address -= blocksize; // go back one block
    off_t old = address % modulo;
    checkedread(old, prevdata, blocksize);
    checkedread(address, originalreaddata, blocksize);
    int n;
    for (n = 0; n < blocksize; ++n) {
        writedata[n] = (i + n) % 256;
    }
    checkedwrite(address, writedata, blocksize);
    // read back the data
    checkedread(address, readbackdata, blocksize);
    // see if it is what we wrote
    int mismatch = 0;
    int corruption = 0;
    for (n = 0; n < MAXBLOCKSIZE; ++n) {
        if (readbackdata [n] != writedata[n]) {
            ++mismatch;
            if (mismatch < 10) {
                printf("Wrote 0x%hhX at address %ld, read back 0x%hhX, original data was 0x%hhX\n",
                    writedata[n], address + n, readbackdata[n], originalreaddata[n]);
            } else if (mismatch == 10) {
                printf("...\n");
            }
        }
    }
    // write back what we read before
    checkedwrite(address, originalreaddata, blocksize);
    // not the first time, check if we corrupted offset/2-size
    checkedread(old, readbackdata, blocksize);
    for (n = 0; n < blocksize; ++n) {
        if (readbackdata [n] != prevdata[n]) {
            ++corruption;
            if (corruption < 10) {
                printf("Writing %hhX to address %ld corrupted address %ld from 0x%hhX to 0x%hhX\n",
                        writedata[n], address + n, old + n, prevdata[n], readbackdata [n]);
            } else if (corruption == 10) {
                printf("...\n");
            }
        }
    }
    if (corruption) {
        // try to write back the original data
        checkedwrite(address, prevdata, blocksize);
    }
    if (mismatch || corruption) {
        exit(-1);
    }
}

int main(int argc, char* argv[]) {
    if (geteuid() != 0) {
        printf("You must be root to run this\n");
        exit(EPERM);
    }
    if (argc != 2) {
        printf("I expect one argument, which must be the absolute filename of a raw block device\n");
        exit(-1);
    }
    filename = argv[1];
    if (strncmp(filename, "/dev/", 5) != 0) {
        printf("%s does not look like a raw block device\n", filename);
        exit(-1);
    }
    int fd = open(filename, O_LARGEFILE|O_SYNC|O_RDWR);
    if (fd < 0) {
        switch (errno) {
            case ENODEV:
            case ENXIO:
            case ENOMEDIUM:
                printf("No device connected at %s\n", filename);
                exit(-1);
            case ENOENT:
                printf("%s does not exist\n", filename);
                exit(-1);
            case EPERM:
            case EACCES:
                printf("You aren't allowed to open %s\n", filename);
                exit(-1);
            default:
                printf("Error opening %s: %s\n", filename, strerror(errno));
                exit(-1);
        }
    }
    // We've got a device, now try and get its size
    unsigned long long totalsize;
    int res = ioctl(fd, BLKGETSIZE64, &totalsize);
    if (res < 0) {
        switch (errno) {
            case ENOTBLK:
            case ENOTSUP:
            case ENOTTY:
#if (EOPNOTSUPP != ENOTSUP)
            case EOPNOTSUPP:
#endif
                printf("%s does not seem to be a block device\n", filename);
                exit(-1);
            default:
                printf("ioctl(BLKGETSIZE64) on  %s: %s\n",
                       filename, strerror(errno));
                exit(-1);
        }
    }
    printf("%s reports its total size as %llu bytes%s\n",
           filename, totalsize, human(totalsize));
    res = ioctl(fd, BLKSSZGET, &blocksize);
    if (res < 0) {
        switch (errno) {
            case ENOTBLK:
            case ENOTSUP:
            case ENOTTY:
#if (EOPNOTSUPP != ENOTSUP)
            case EOPNOTSUPP:
#endif
                printf("%s does not seem to be a block device\n", filename);
                exit(-1);
            default:
                printf("ioctl(BLKSSZGET) on  %s: %s\n",
                       filename, strerror(errno));
                exit(-1);
        }
    }
    printf("%s reports its sector size as %llu bytes%s\n", filename,
           blocksize, human(blocksize));
    unsigned char buffer[MAXBLOCKSIZE];
    if (close(fd) != 0) {
        printf("Error closing %s: %s\n", filename, strerror(errno));
        exit(-1);
    }
    // Read the Master Boot Record:
    checkedread(0, buffer, MINBLOCKSIZE);
    /* Partition type is stored at block 0 address 450 (decimal)
     * A type of 0xEE indicates GPT partitioning.
     */
    if (buffer[450] == 0xEE) {
        size_t size;
        printf("%s appears to have GPT partitioning\n", filename);
        for (size = MINBLOCKSIZE; size <= MAXBLOCKSIZE; size *= 2) {
            checkedread(size, buffer, size);
            if (*(unsigned long long *)buffer == 0x5452415020494645ULL) {
                break; // found a GPT header
            }
        }
        blocksize = size;
        if (size > MAXBLOCKSIZE) {
            printf("Could not find GPT header on %s\n", filename);
        } else {
            printf("GPT header sector size is %lu\n", blocksize);
            printf("GPT main header on %s is at address %llu\n",
                   filename, blocksize);
            printf("GPT main header reports its own address as %llu\n",
                   *(off_t *)(buffer + 24) * blocksize);
            printf("GPT main header reports first usable block as %llu\n",
                   *(off_t *)(buffer + 40) * blocksize);
            printf("GPT main header reports last usable block as %llu\n",
                   *(off_t *)(buffer + 48) * blocksize);
            off_t backup = *(off_t *)(buffer + 32) * blocksize;
            off_t ptable = *(off_t *)(buffer + 72) * blocksize;
            int pcount = *(u_int32_t *)(buffer + 80);
            int psize = *(u_int32_t *)(buffer + 84);
            checkedread(ptable, buffer, blocksize);
            printf("GPT main partition table:\n");
            partitions(ptable, pcount, psize);
            printf("GPT main header reports backup header address as %llu\n",
                   backup);
            checkedread(backup, buffer, blocksize);
            if (*(unsigned long long *)buffer != 0x5452415020494645ULL) {
                printf("GPT backup header invalid signature 0x%lX\n",
                       *(unsigned long long *)buffer);
            } else {
                printf("GPT backup header reports its own address as %lu\n",
                    *(off_t *)(buffer + 24) * blocksize);
                backup = *(off_t *)(buffer + 32) * size;
                printf("GPT backup header reports main header address as %lu\n",
                    backup);
                printf("GPT backup header reports first usable block as %llu\n",
                    *(off_t *)(buffer + 40) * blocksize);
                printf("GPT backup header reports last usable block as %llu\n",
                    *(off_t *)(buffer + 48) * blocksize);
                off_t ptable = *(off_t *)(buffer + 72) * blocksize;
                u_int32_t pcount = *(u_int32_t *)(buffer + 80);
                u_int32_t psize = *(u_int32_t *)(buffer + 84);
                checkedread(ptable, buffer, blocksize);
                printf("GPT backup partition table:\n");
                partitions(ptable, pcount, psize);
            }
        }
    }
    FILE * pm = fopen("/proc/mounts", "r");
    if (pm == NULL) {
        printf("cannot open /proc/mounts: %s\n", strerror(errno));
        exit(-1);
    }
    size_t len = strlen(filename);
    while (fgets(buffer, MAXBLOCKSIZE, pm) != NULL) {
        if (strncmp(filename, buffer, len) == 0) {
            printf("Read/write size test cannot safely be done because\n");
            printf("%s has a mounted partition\n", filename);
            exit(0);
        }
        // just in case /proc/mounts has some very long lines
        while (buffer[strlen(buffer) - 1] != '\n')
        {
            if (fgets(buffer, MAXBLOCKSIZE, pm) == NULL) {
                goto errorcheck;
            }
        }
    }
    errorcheck:
    // NULL return can be EOF or error
    if (!feof(pm)) {
        printf("Error reading /proc/mounts: %s\n", strerror(errno));
        exit(-1);
    }

    printf("The read/write size test will check the real amount of storage\n");
    printf("on the device. It tries not to corrupt the data on the device\n");
    printf("but this cannot be guaranteed. It should only be run when\n");
    printf("you suspect that the reported size of a new device is wrong.\n");
    printf("Do you want to do a read/write size test (Y/N)?");
    if (confirm() == 0) { exit(0); }
    printf("Are you sure?");
    if (confirm() == 0) { exit(0); }

    /* We walk up the device testing addresses which are one sector
     * less than powers of 2, looking for these possible errors:
     * 1. Cannot read from address
     * 2. Cannot write to address
     * 3. Can write to address but data read back is wrong
     * 4' Writing to address overwrites a different address
     *
     * In case 4, we can't check everywhere, but we check modulo the largest
     * power of two less than the address to which we tried to write:
     * this corresponds to the device ignoring the highest bit of the address.
     */
    off_t offset = 1024*1024; // Start at 1 Mibyte
    int i;
    for (i = 0; offset <= totalsize; ++i) {
        readbacktest(offset, offset / 2, i);
        offset = offset * 2;
    }
    if (offset != totalsize) {
        // totalsize isn't a power of 2
        // walk up halving the distance to totalsize
        offset = offset / 2;
        off_t modulo = offset;
        while (totalsize - offset > 1024*1024) {
            ++i;
            offset = (offset + totalsize) / 2;
            readbacktest(offset, modulo, i);
        }
    }
    exit(0);
}
