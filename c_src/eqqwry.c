#include <stdio.h>
#include <string.h>
#include "erl_nif.h"

#ifdef __GNUC__
    #include <stdbool.h>
#else
    #define bool int
    #define true 1
    #define false 0
#endif

#ifndef uni32_t
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned uint32_t;
#endif

#define INDEX_SIZE 7

typedef struct {
    const uint8_t* buffer;
    uint32_t length;
    uint32_t count;
} ipdb;

typedef struct {
    uint32_t lower;
    uint32_t upper;
    const char* zone;
    const char* area;
} ipdb_item;

static ipdb *p_ipdb = NULL;

static uint8_t*
readfile(const char *path, uint32_t *length) {
    uint8_t *buffer = NULL;
    FILE *fp = fopen(path, "rb");
    if (fp) {
        fseek(fp, 0, SEEK_END);
        *length = ftell(fp);
        buffer = (uint8_t*)malloc(*length);

        fseek(fp, 0, SEEK_SET);
        fread(buffer, *length, 1, fp);
        fclose(fp);
    }
    return buffer;
}

static uint32_t
get_3b(const char *mem) {
    return 0x00ffffff & *(uint32_t*)(mem);
}

static bool
qqwry_find(const ipdb *db, ipdb_item *item, uint32_t ip) {
    char *ptr = (char*)db->buffer;
    char *p = ptr + *(uint32_t*)ptr;

    uint32_t low = 0;
    uint32_t high = db->count;
    while (low < high - 1) {
        uint32_t mid = low + (high - low)/2;
        if (ip < *(uint32_t*)(p + mid * INDEX_SIZE))
            high = mid;
        else
            low = mid;
    }

    uint32_t index = low;
    if (index >= db->count)
        return false;

    uint32_t temp = get_3b(p + INDEX_SIZE * index + 4);
    char *offset = ptr + temp + 4;

    item->lower = *(uint32_t*)(p + INDEX_SIZE * index);
    item->upper = *(uint32_t*)(ptr + temp);

    if (0x01 == *offset)
        offset = ptr + get_3b(offset + 1);

    if (0x02 == *offset) {
        item->zone = (const char*)(ptr + get_3b(offset + 1));
        offset += 4;
    } else {
        item->zone = (const char*)offset;
        offset += strlen(offset) + 1;
    }

    if (0x02 == *offset)
        item->area = (const char*)(ptr + get_3b(offset + 1));
    else
        item->area = (const char*)offset;

    return true;
}

static ERL_NIF_TERM
lookup(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
    if (argc != 1)
        return enif_make_badarg(env);

    uint32_t ip;
    if (!enif_get_uint(env, argv[0], &ip))
        return enif_make_badarg(env);

    ipdb_item item;
    if (qqwry_find(p_ipdb, &item ,ip)) {
        ERL_NIF_TERM lower = enif_make_int(env, item.lower);
        ERL_NIF_TERM upper = enif_make_int(env, item.upper);
        ERL_NIF_TERM zone = enif_make_string(env, item.zone, ERL_NIF_LATIN1);
        ERL_NIF_TERM area = enif_make_string(env, item.area, ERL_NIF_LATIN1);
        return enif_make_tuple4(env, lower, upper, zone, area);
    } else {
        return enif_make_atom(env, "not_found");
    }
}

static int
load(ErlNifEnv *env, void **priv, ERL_NIF_TERM load_info) {
    if (!enif_is_list(env, load_info))
        return -1;

    char filename[512];
    if (0 == enif_get_string(env, load_info, filename, sizeof(filename), ERL_NIF_LATIN1))
        return -1;

    if (p_ipdb == NULL) {
        p_ipdb = (ipdb*)malloc(sizeof(ipdb));
        uint32_t length = 0;
        uint8_t *buffer = readfile(filename, &length);
        if (buffer == NULL)
            return -1;

        p_ipdb->length = length;
        p_ipdb->buffer = buffer;
        uint32_t *post = (uint32_t*)buffer;
        uint32_t first = *post;
        uint32_t last = *(post + 1);
        p_ipdb->count = (last - first)/INDEX_SIZE + 1;
    }

    return 0;
}


static ErlNifFunc
nif_funcs[] = {
    {"lookup_nif", 1, lookup}
};

ERL_NIF_INIT(eqqwry, nif_funcs, &load, NULL, NULL, NULL)
