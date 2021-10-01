#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADDRESS_BITS 32
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
    uint8_t valid;
    uint32_t tag;
} cache_line_t;

typedef struct {
    cache_line_t *lines;
    uintptr_t size;
    // FIFO tail index
    uintptr_t tail_index;
} cache_t;

typedef struct {
    uint32_t size;
    uint32_t block_count;
    uint32_t offset_bits;
    uint32_t index_bits;
    uint32_t tag_bits;
} cache_context_t;

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
        char *ty = strtok(string, " \n");
        if (strcmp(ty, "I") == 0) {
            access.accessType = INSTRUCTION;
        } else if (strcmp(ty, "D") == 0) {
            access.accessType = DATA;
        } else {
            printf("Unkown access type\n");
            exit(0);
        }

        /* Get the access address */
        char *address = strtok(NULL, " \n");
        access.address = (uint32_t)strtoul(address, NULL, 16);

        return access;
    }

    /* If there are no more entries in the file,
     * return an address 0 that will terminate the infinite loop in main
     */
    access.address = 0;
    return access;
}

uint32_t extract_bits(uint32_t val, uint32_t startBit, uint32_t len) {
    uint32_t mask = ((1U << len) - 1U) << startBit;
    return (val & mask) >> startBit;
}

void cache_read(cache_t *cache, cache_context_t ctx, cache_map_t mapping,
                mem_access_t access, cache_stat_t *stat) {
    stat->accesses += 1;

    uint32_t index =
        extract_bits(access.address, ctx.offset_bits, ctx.index_bits);
    uint32_t tag = extract_bits(access.address,
                                ctx.offset_bits + ctx.index_bits, ctx.tag_bits);

    if (mapping == DIRECT_MAPPING) {
        if (index >= cache->size) {
            printf("Invalid cache index\n");
            exit(1);
        }

        cache_line_t *line = &cache->lines[index];
        if (line->valid && line->tag == tag) {
            stat->hits += 1;
        } else {
            line->valid = 1;
            line->tag = tag;
        }
    } else {
        for (uint32_t i = 0; i < ctx.size; i++) {
            cache_line_t *line = &cache->lines[i];
            if (line->valid && line->tag == tag) {
                stat->hits += 1;
                return;
            }
        }

        cache_line_t *line = &cache->lines[cache->tail_index];
        line->valid = 1;
        line->tag = tag;
        cache->tail_index = (cache->tail_index + 1) % cache->size;
    }
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

    if (cache_org == SPLIT) {
        cache_size /= 2;
    }

    uint32_t block_count = cache_size / BLOCK_SIZE;
    uint32_t offset_bits = (uint32_t)log2(BLOCK_SIZE);

    uint32_t index_bits;
    if (cache_mapping == DIRECT_MAPPING) {
        index_bits = (uint32_t)log2(block_count);
    } else {
        index_bits = 0;
    }

    uint32_t tag_bits = ADDRESS_BITS - index_bits - offset_bits;

    cache_context_t cache_ctx = {.size = cache_size,
                                 .block_count = block_count,
                                 .offset_bits = offset_bits,
                                 .index_bits = index_bits,
                                 .tag_bits = tag_bits};

    cache_t *instr_cache = malloc(sizeof(cache_t));
    instr_cache->lines = calloc(block_count, sizeof(cache_line_t));
    instr_cache->size = block_count;
    instr_cache->tail_index = 0;

    cache_t *data_cache;
    if (cache_org == UNIFIED) {
        data_cache = instr_cache;
    } else {
        data_cache = malloc(sizeof(cache_t));
        data_cache->lines = calloc(block_count, sizeof(cache_line_t));
        data_cache->size = block_count;
        data_cache->tail_index = 0;
    }

    /* Open the file mem_trace.txt to read memory accesses */
    FILE *ptr_file = fopen("mem_trace2.txt", "r");
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

        if (access.accessType == INSTRUCTION) {
            cache_read(instr_cache, cache_ctx, cache_mapping, access,
                       &cache_statistics);
        } else {
            cache_read(data_cache, cache_ctx, cache_mapping, access,
                       &cache_statistics);
        }
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

    return 0;
}
