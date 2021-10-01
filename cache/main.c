#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE 64

typedef enum { DIRECT_MAPPING, FULLY_ASSOCIATIVE } cache_map_t;
typedef enum { UNIFIED, SPLIT } cache_org_t;
typedef enum { INSTRUCTION, DATA } access_t;

typedef struct {
    uint32_t address;
    access_t accessType;
} mem_access_t;

typedef struct {
    uint64_t accesses;
    uint64_t hits;
    // You can declare additional statistics if
    // you like, however you are now allowed to
    // remove the accesses or hits
} cache_stat_t;

typedef struct {
    uint8_t data[BLOCK_SIZE];
} cache_block_t;

typedef struct {
    cache_block_t *blocks;
    uintptr_t size;
} cache_t;

char *strsep(char **stringp, const char *delim) {
    char *start = *stringp;
    char *p;

    p = (start != NULL) ? strpbrk(start, delim) : NULL;

    if (p == NULL) {
        *stringp = NULL;
    } else {
        *p = '\0';
        *stringp = p + 1;
    }

    return start;
}

#ifdef _WIN32
FILE *fopen(const char *filename, const char *mode) {
    FILE *file;
    fopen_s(&file, filename, mode);
    return file;
}
#endif

/* Reads a memory access from the trace file and returns
 * 1) access type (instruction or data access
 * 2) memory address
 */
mem_access_t read_transaction(FILE *ptr_file) {
    char buf[1000];
    char *string = buf;
    mem_access_t access;

    if (fgets(buf, 1000, ptr_file) != NULL) {

        /* Get the access type */
        char *ty = strsep(&string, " \n");
        if (strcmp(ty, "I") == 0) {
            access.accessType = INSTRUCTION;
        } else if (strcmp(ty, "D") == 0) {
            access.accessType = DATA;
        } else {
            printf("Unkown access type\n");
            exit(0);
        }

        /* Get the access address */
        char *address = strsep(&string, " \n");
        access.address = (uint32_t)strtoul(address, NULL, 16);

        return access;
    }

    /* If there are no more entries in the file,
     * return an address 0 that will terminate the infinite loop in main
     */
    access.address = 0;
    return access;
}

int main(int argc, char **argv) {
    // DECLARE CACHES AND COUNTERS FOR THE STATS HERE

    uint32_t cache_size;
    cache_map_t cache_mapping;
    cache_org_t cache_org;

    // USE THIS FOR YOUR CACHE STATISTICS
    cache_stat_t cache_statistics;

    // Reset statistics:
    memset(&cache_statistics, 0, sizeof(cache_stat_t));

    /* Read command-line parameters and initialize:
     * cache_size, cache_mapping and cache_org variables
     */

    if (argc != 4) { /* argc should be 2 for correct execution */
        printf(
            "Usage: ./cache_sim [cache size: 128-4096] [cache mapping: dm|fa] "
            "[cache organization: uc|sc]\n");
        exit(1);
    } else {
        /* argv[0] is program name, parameters start with argv[1] */

        /* Set cache size */
        cache_size = (uint32_t)strtoul(argv[1], NULL, 10);
        if (cache_size % BLOCK_SIZE != 0) {
            printf("Cache size must be a multiple of %d\n", BLOCK_SIZE);
            exit(1);
        }

        /* Set Cache Mapping */
        if (strcmp(argv[2], "dm") == 0) {
            cache_mapping = DIRECT_MAPPING;
        } else if (strcmp(argv[2], "fa") == 0) {
            cache_mapping = FULLY_ASSOCIATIVE;
        } else {
            printf("Unknown cache mapping\n");
            exit(1);
        }

        /* Set Cache Organization */
        if (strcmp(argv[3], "uc") == 0) {
            cache_org = UNIFIED;
        } else if (strcmp(argv[3], "sc") == 0) {
            cache_org = SPLIT;
        } else {
            printf("Unknown cache organization\n");
            exit(1);
        }
    }

    uintptr_t size = cache_size / BLOCK_SIZE;
    cache_t cache = {.blocks = calloc(size, sizeof(cache_block_t)),
                     .size = size};

    /* Open the file mem_trace.txt to read memory accesses */
    FILE *ptr_file = fopen("mem_trace1.txt", "r");
    if (!ptr_file) {
        printf("Unable to open the trace file\n");
        exit(1);
    }

    /* Loop until whole trace file has been read */
    mem_access_t access;
    while (1) {
        access = read_transaction(ptr_file);
        // If no transactions left, break out of loop
        if (access.address == 0)
            break;
        printf("%d %x\n", access.accessType, access.address);
        /* Do a cache access */
        // ADD YOUR CODE HERE
    }

    /* Print the statistics */
    // DO NOT CHANGE THE FOLLOWING LINES!
    printf("\nCache Statistics\n");
    printf("-----------------\n\n");
    printf("Accesses: %llu\n", cache_statistics.accesses);
    printf("Hits:     %llu\n", cache_statistics.hits);
    printf("Hit Rate: %.4f\n",
           (double)cache_statistics.hits / (double)cache_statistics.accesses);
    // You can extend the memory statistic printing if you like!

    /* Close the trace file */
    fclose(ptr_file);

    free(cache.blocks);

    return 0;
}
