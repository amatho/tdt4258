#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ADDRESS_BITS = 32 };
enum { BLOCK_SIZE = 64 };

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

// A cache line
typedef struct {
    // Whether the cache line contains valid data or not
    uint8_t valid;
    // The tag of the cache line
    uint32_t tag;
} cache_line_t;

// The cache data structure
typedef struct {
    // An array of cache lines
    cache_line_t *lines;
    // The size of the cache lines array
    uintptr_t size;
    // The tail index for the FIFO queue when the cache is Fully Associative
    uintptr_t tail_index;
} cache_t;

// Context information for the cache(s)
typedef struct {
    // The instruction cache
    cache_t *instr_cache;
    // The data cache
    cache_t *data_cache;
    // The cache mapping
    cache_map_t mapping;
    // The cache organization
    cache_org_t organization;

    // Number of bits to use for the offset
    uint32_t offset_bits;
    // Number of bits to use for the index
    uint32_t index_bits;
    // Number of bits to use for the tag
    uint32_t tag_bits;
} cache_context_t;

// fopen is deprecated on Windows in favor of fopen_s
#ifdef _WIN32
FILE *fopen(const char *filename, const char *mode) {
    FILE *file;
    fopen_s(&file, filename, mode);
    return file;
}
#endif

cache_context_t create_context(uint32_t cache_size,
                               const cache_map_t cache_mapping,
                               const cache_org_t cache_org) {
    if (cache_org == SPLIT) {
        cache_size /= 2;
    }

    const uint32_t line_count = cache_size / BLOCK_SIZE;
    const uint32_t offset_bits = (uint32_t)log2(BLOCK_SIZE);
    const uint32_t index_bits =
        cache_mapping == DIRECT_MAPPING ? (uint32_t)log2(line_count) : 0;
    const uint32_t tag_bits = ADDRESS_BITS - index_bits - offset_bits;

    cache_t *const instr_cache = malloc(sizeof(cache_t));
    instr_cache->lines = calloc(line_count, sizeof(cache_line_t));
    instr_cache->size = line_count;
    instr_cache->tail_index = 0;

    cache_t *data_cache;
    if (cache_org == UNIFIED) {
        data_cache = instr_cache;
    } else {
        data_cache = malloc(sizeof(cache_t));
        data_cache->lines = calloc(line_count, sizeof(cache_line_t));
        data_cache->size = line_count;
        data_cache->tail_index = 0;
    }

    const cache_context_t cache_ctx = {.instr_cache = instr_cache,
                                       .data_cache = data_cache,
                                       .mapping = cache_mapping,
                                       .organization = cache_org,
                                       .offset_bits = offset_bits,
                                       .index_bits = index_bits,
                                       .tag_bits = tag_bits};

    return cache_ctx;
}

// Reads a memory access from the trace file and returns
//  * access type (instruction or data access)
//  * memory address
mem_access_t read_transaction(FILE *ptr_file) {
    char buf[1000];
    char *string = buf;
    mem_access_t access;

    if (fgets(buf, 1000, ptr_file) != NULL) {

        // Get the access type
        const char *ty = strtok(string, " \n");
        if (strcmp(ty, "I") == 0) {
            access.accessType = INSTRUCTION;
        } else if (strcmp(ty, "D") == 0) {
            access.accessType = DATA;
        } else {
            printf("Unkown access type\n");
            exit(0);
        }

        // Get the access address
        const char *address = strtok(NULL, " \n");
        access.address = (uint32_t)strtoul(address, NULL, 16);

        return access;
    }

    // If there are no more entries in the file, return an address 0 that will
    // terminate the infinite loop in main
    access.address = 0;
    return access;
}

// Extracts the bits starting at `startBit` with length `len` and returns them
// as an integer
uint32_t extract_bits(const uint32_t val, const uint32_t startBit,
                      const uint32_t len) {
    uint32_t mask = ((1U << len) - 1U) << startBit;
    return (val & mask) >> startBit;
}

void cache_read(const cache_context_t ctx, const mem_access_t access,
                cache_stat_t *const stat) {
    stat->accesses += 1;

    uint32_t index =
        extract_bits(access.address, ctx.offset_bits, ctx.index_bits);
    uint32_t tag = extract_bits(access.address,
                                ctx.offset_bits + ctx.index_bits, ctx.tag_bits);

    cache_t *cache;
    if (access.accessType == INSTRUCTION) {
        cache = ctx.instr_cache;
    } else {
        cache = ctx.data_cache;
    }

    if (ctx.mapping == DIRECT_MAPPING) {
        // Make sure the index is in bounds
        if (index >= cache->size) {
            printf("Invalid cache index\n");
            exit(1);
        }

        // Get the cache line associated with this index
        cache_line_t *line = &cache->lines[index];
        if (line->valid && line->tag == tag) {
            stat->hits += 1;
        } else {
            // Replace the cached value
            line->valid = 1;
            line->tag = tag;
        }
    } else {
        // Mapping is Fully associative

        // Loop through the cache lines and look for a matching tag
        for (uintptr_t i = 0; i < cache->size; i++) {
            cache_line_t *line = &cache->lines[i];
            if (line->valid && line->tag == tag) {
                stat->hits += 1;
                return;
            }
        }

        // A matching tag was not found, so we insert it in the queue
        cache_line_t *line = &cache->lines[cache->tail_index];
        line->valid = 1;
        line->tag = tag;
        // Increment the tail index of the queue with wrap-around
        cache->tail_index = (cache->tail_index + 1) % cache->size;
    }
}

int main(const int argc, const char **argv) {
    uint32_t cache_size;
    cache_map_t cache_mapping;
    cache_org_t cache_org;

    // Read command-line parameters and initialize:
    // cache_size, cache_mapping and cache_org variables

    // argc should be 2 for correct execution
    if (argc != 4) {
        printf(
            "Usage: ./cache_sim [cache size: 128-4096] [cache mapping: dm|fa] "
            "[cache organization: uc|sc]\n");
        exit(1);
    } else {
        // argv[0] is program name, parameters start with argv[1]

        // Set cache size
        cache_size = (uint32_t)strtoul(argv[1], NULL, 10);

        // Set cache mapping
        if (strcmp(argv[2], "dm") == 0) {
            cache_mapping = DIRECT_MAPPING;
        } else if (strcmp(argv[2], "fa") == 0) {
            cache_mapping = FULLY_ASSOCIATIVE;
        } else {
            printf("Unknown cache mapping\n");
            exit(1);
        }

        // Set cache organization
        if (strcmp(argv[3], "uc") == 0) {
            cache_org = UNIFIED;
        } else if (strcmp(argv[3], "sc") == 0) {
            cache_org = SPLIT;
        } else {
            printf("Unknown cache organization\n");
            exit(1);
        }
    }

    // Create the cache context from the user input
    const cache_context_t cache_ctx =
        create_context(cache_size, cache_mapping, cache_org);

    // Open the file mem_trace.txt to read memory accesses
    FILE *ptr_file = fopen("mem_trace.txt", "r");
    if (!ptr_file) {
        printf("Unable to open the trace file\n");
        exit(1);
    }

    // Loop until whole trace file has been read
    mem_access_t access;
    cache_stat_t cache_stat;
    while (1) {
        access = read_transaction(ptr_file);
        // If no transactions left, break out of loop
        if (access.address == 0) {
            break;
        }

        printf("%d %x\n", access.accessType, access.address);

        // Perform a cache read
        cache_read(cache_ctx, access, &cache_stat);
    }

    // Print the statistics
    // DO NOT CHANGE THE FOLLOWING LINES!
    printf("\nCache Statistics\n");
    printf("-----------------\n\n");
    printf("Accesses: %lu\n", cache_stat.accesses);
    printf("Hits:     %lu\n", cache_stat.hits);
    printf("Hit Rate: %.4f\n",
           (double)cache_stat.hits / (double)cache_stat.accesses);
    // You can extend the memory statistic printing if you like!

    // Close the trace file
    fclose(ptr_file);

    free(cache_ctx.instr_cache->lines);
    free(cache_ctx.instr_cache);

    if (cache_ctx.organization == SPLIT) {
        free(cache_ctx.data_cache->lines);
        free(cache_ctx.data_cache);
    }

    return 0;
}
