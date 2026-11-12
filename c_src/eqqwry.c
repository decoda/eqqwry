/* eqqwry NIF:纯真 QQWry.Dat(IP 地理位置数据库)解析。
 *
 * 数据文件为纯真官方 dat 格式(字符串为 GBK 编码),查询时经内嵌码表
 * (c_src/gbk_table.h)转换为 UTF-8 返回。为保证跨平台可移植:
 *   - 所有整数字段按小端逐字节组装,不做未对齐指针读取;
 *   - 解析过程对每个偏移做范围校验,数据文件损坏时安全返回 not_found。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "erl_nif.h"
#include "gbk_table.h"

#define INDEX_SIZE 7
#define MAX_REDIRECT 8 /* 0x01 重定向的最大跟随层数(正常数据仅 1 层) */

typedef struct {
    const uint8_t *buffer; /* 数据文件全文 */
    size_t length;         /* 文件字节数 */
    size_t count;          /* 索引条数 = (last - first) / INDEX_SIZE + 1 */
    size_t first;          /* 索引区首偏移(文件头第 1 个 uint32) */
} ipdb;

typedef struct {
    uint32_t lower;  /* IP 段起始(小端读出) */
    uint32_t upper;  /* IP 段结束 */
    const uint8_t *zone; /* GBK 字符串,NUL 结尾 */
    const uint8_t *area; /* GBK 字符串,NUL 结尾 */
} ipdb_item;

static ipdb *p_ipdb = NULL;

/* ---- 小端整数读取(对齐安全,兼支持大端主机) ---- */

static uint32_t
get_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* 索引中的 3 字节记录偏移 */
static uint32_t
get_u24le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

/* ---- 文件读取 ---- */

static uint8_t *
readfile(const char *path, size_t *length) {
    uint8_t *buffer = NULL;
    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return NULL;

    if (fseek(fp, 0, SEEK_END) != 0)
        goto fail;
    {
        long sz = ftell(fp);
        if (sz <= 0 || fseek(fp, 0, SEEK_SET) != 0)
            goto fail;
        *length = (size_t)sz;
    }

    buffer = (uint8_t *)malloc(*length);
    if (buffer == NULL)
        goto fail;

    if (fread(buffer, 1, *length, fp) != *length) {
        free(buffer);
        buffer = NULL;
    }
fail:
    fclose(fp);
    return buffer;
}

/* ---- GBK → UTF-8 转换(查询时执行,仅几十字节开销) ---- */

/* GBK 双字节 → Unicode 码点;非法序列返回 0 且置失败标志 */
static int
gbk_char(const uint8_t *s, size_t left, uint32_t *ucs) {
    uint8_t hi = s[0];
    uint8_t lo;
    int col;

    /* ASCII 直通 */
    if (hi < 0x80) {
        *ucs = hi;
        return 1;
    }
    if (left < 2 || hi < GBK_HI_MIN || hi > GBK_HI_MAX)
        return 0; /* 非法/截断序列 */
    lo = s[1];
    if (lo >= 0x40 && lo <= 0x7E)
        col = lo - 0x40;
    else if (lo >= 0x80 && lo <= 0xFE)
        col = lo - 0x41;
    else
        return 0;
    *ucs = gbk_ucs_table[(hi - GBK_HI_MIN) * GBK_COL_MAX + col];
    return (*ucs != 0) ? 2 : 0; /* 2 表示消费两个字节 */
}

static size_t
ucs_to_utf8(uint32_t u, uint8_t *out) {
    if (u < 0x80) {
        out[0] = (uint8_t)u;
        return 1;
    }
    if (u < 0x800) {
        out[0] = (uint8_t)(0xC0 | (u >> 6));
        out[1] = (uint8_t)(0x80 | (u & 0x3F));
        return 2;
    }
    if (u > 0xFFFF) {
        /* GBK 码表只覆盖 BMP,理论不会走到;防御性兜底为 U+FFFD */
        u = 0xFFFD;
    }
    /* GBK 码表仅覆盖 BMP(≤ 0xFFFF),3 字节编码足够 */
    out[0] = (uint8_t)(0xE0 | (u >> 12));
    out[1] = (uint8_t)(0x80 | ((u >> 6) & 0x3F));
    out[2] = (uint8_t)(0x80 | (u & 0x3F));
    return 3;
}

/* Unicode 码点 → UTF-8 字节数。
 * 必须与 ucs_to_utf8 的编码长度严格一致,否则返回值会多出未初始化字节。 */
static size_t
utf8_len(uint32_t u) {
    if (u < 0x80)
        return 1;
    if (u < 0x800)
        return 2;
    return 3;
}

/* 将 buffer 内 [src, maxlen) 的 GBK 字符串(NUL 结尾)转为 UTF-8 binary。
 * 成功返回 0 并写出 *term;非法序列以 U+FFFD 兜底,保证总能返回结果。 */
static int
make_utf8_binary(ErlNifEnv *env, const uint8_t *src, size_t maxlen,
                 ERL_NIF_TERM *term) {
    const uint8_t *nul = (const uint8_t *)memchr(src, 0, maxlen);
    size_t inlen = nul ? (size_t)(nul - src) : maxlen;
    size_t outlen = 0, i = 0;
    ErlNifBinary bin;

    /* 第一遍:计算输出长度(按真实码点算 UTF-8 字节数) */
    while (i < inlen) {
        uint32_t u;
        int n = gbk_char(src + i, inlen - i, &u);
        if (n == 0) { /* 非法序列:U+FFFD 兜底 */
            outlen += 3;
            i += 1;
        } else {
            outlen += utf8_len(u);
            i += (size_t)n;
        }
    }

    if (!enif_alloc_binary(outlen, &bin))
        return -1;

    /* 第二遍:填充 */
    {
        uint8_t *out = bin.data;
        size_t o = 0;
        i = 0;
        while (i < inlen) {
            uint32_t u;
            int n = gbk_char(src + i, inlen - i, &u);
            if (n == 0) {
                u = 0xFFFD;
                o += (size_t)ucs_to_utf8(u, out + o);
                i += 1;
            } else if (n == 1) {
                out[o++] = (uint8_t)u;
                i += 1;
            } else {
                o += (size_t)ucs_to_utf8(u, out + o);
                i += 2;
            }
        }
    }
    *term = enif_make_binary(env, &bin);
    return 0;
}

/* ---- QQWry 记录区解析 ---- */

/* 限制在文件内的指针校验 */
static int
in_bounds(const ipdb *db, size_t off, size_t extra) {
    return off + extra <= db->length;
}

static int
qqwry_find(const ipdb *db, ipdb_item *item, uint32_t ip) {
    const uint8_t *base = db->buffer;
    /* 索引区首偏移在 load 时已校验并缓存 */
    size_t first = db->first;
    size_t low = 0, high = db->count, index;
    size_t rec_off, off;
    int hops;

    if (db->count == 0)
        return 0;

    /* 二分:找满足 索引IP <= ip 的最大索引(索引按起始 IP 升序) */
    while (low < high - 1) {
        size_t mid = low + (high - low) / 2;
        uint32_t ip_mid = get_u32le(base + first + mid * INDEX_SIZE);
        if (ip < ip_mid)
            high = mid;
        else
            low = mid;
    }
    index = low;

    if (!in_bounds(db, first + index * INDEX_SIZE, INDEX_SIZE))
        return 0;
    item->lower = get_u32le(base + first + index * INDEX_SIZE);
    if (ip < item->lower)
        return 0; /* 低于库内最小 IP */

    rec_off = get_u24le(base + first + index * INDEX_SIZE + 4);
    if (!in_bounds(db, rec_off, 4))
        return 0;
    item->upper = get_u32le(base + rec_off);
    if (ip > item->upper)
        return 0; /* 高于该段上界(落在区间空洞) */

    /* 0x01 重定向:记录头后跟 3 字节偏移,指向实际字符串。
     * 正常数据为单层,循环仅作损坏数据防御。 */
    off = rec_off + 4;
    for (hops = 0; hops < MAX_REDIRECT; hops++) {
        size_t next;
        if (!in_bounds(db, off, 1))
            return 0;
        if (base[off] != 0x01)
            break;
        if (!in_bounds(db, off + 1, 3))
            return 0;
        next = get_u24le(base + off + 1);
        if (!in_bounds(db, next, 1))
            return 0;
        off = next;
    }
    if (!in_bounds(db, off, 1))
        return 0;
    if (base[off] == 0x01)
        return 0; /* 重定向层数超限(数据损坏) */

    /* 国家/地区(zone):0x02 表示指向共享字符串,否则为内联字符串 */
    if (base[off] == 0x02) {
        size_t zo;
        if (!in_bounds(db, off + 1, 3))
            return 0;
        zo = get_u24le(base + off + 1);
        if (!in_bounds(db, zo, 1))
            return 0;
        item->zone = base + zo;
        off += 4;
    } else {
        const uint8_t *nul;
        item->zone = base + off;
        nul = (const uint8_t *)memchr(item->zone, 0, db->length - (size_t)(item->zone - base));
        if (nul == NULL)
            return 0; /* 无 NUL 终止,数据损坏 */
        off = (size_t)(nul - base) + 1;
    }

    /* 区域(area):同样支持共享/内联两种模式 */
    if (!in_bounds(db, off, 1))
        return 0;
    if (base[off] == 0x02) {
        size_t ao;
        if (!in_bounds(db, off + 1, 3))
            return 0;
        ao = get_u24le(base + off + 1);
        if (!in_bounds(db, ao, 1))
            return 0;
        item->area = base + ao;
    } else {
        item->area = base + off;
        if (memchr(item->area, 0, db->length - (size_t)(item->area - base)) == NULL)
            return 0;
    }
    return 1;
}

/* ---- NIF ---- */

static ERL_NIF_TERM
lookup(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
    uint32_t ip;
    ipdb_item item;
    ERL_NIF_TERM zone, area;

    if (p_ipdb == NULL || argc != 1)
        return enif_make_badarg(env);

    if (!enif_get_uint(env, argv[0], &ip))
        return enif_make_badarg(env);

    if (!qqwry_find(p_ipdb, &item, ip))
        return enif_make_atom(env, "not_found");

    if (make_utf8_binary(env, item.zone, p_ipdb->length - (size_t)(item.zone - p_ipdb->buffer), &zone) != 0)
        return enif_raise_exception(env, enif_make_atom(env, "out_of_memory"));
    if (make_utf8_binary(env, item.area, p_ipdb->length - (size_t)(item.area - p_ipdb->buffer), &area) != 0)
        return enif_raise_exception(env, enif_make_atom(env, "out_of_memory"));

    /* {Lower, Upper, Zone, Area};Lower/Upper 为 0~2^32-1 的无符号整数 */
    return enif_make_tuple4(env,
                            enif_make_uint(env, item.lower),
                            enif_make_uint(env, item.upper),
                            zone, area);
}

/* load_info:由 Erlang 侧 init/0 传入的 dat 文件路径 */
static int
load(ErlNifEnv *env, void **priv, ERL_NIF_TERM load_info) {
    char filename[512];
    uint8_t *buffer;
    size_t length = 0;
    size_t first, last;

    (void)priv;

    if (!enif_is_list(env, load_info))
        return -1;
    if (enif_get_string(env, load_info, filename, sizeof(filename), ERL_NIF_UTF8) <= 0)
        return -1;

    if (p_ipdb != NULL)
        return 0; /* 已加载(模块热加载等场景),复用现有数据 */

    p_ipdb = (ipdb *)malloc(sizeof(ipdb));
    if (p_ipdb == NULL)
        return -1;

    buffer = readfile(filename, &length);
    if (buffer == NULL) {
        free(p_ipdb);
        p_ipdb = NULL;
        return -1;
    }
    if (length < 8) { /* 至少要有文件头 */
        free(buffer);
        free(p_ipdb);
        p_ipdb = NULL;
        return -1;
    }

    first = get_u32le(buffer);
    last = get_u32le(buffer + 4);
    /* 校验索引区范围,防止损坏文件导致越界或死循环 */
    if (first < 8 || last < first || last + INDEX_SIZE > length ||
        last - first < INDEX_SIZE) {
        free(buffer);
        free(p_ipdb);
        p_ipdb = NULL;
        return -1;
    }

    p_ipdb->buffer = buffer;
    p_ipdb->length = length;
    p_ipdb->first = first;
    p_ipdb->count = (last - first) / INDEX_SIZE + 1;
    return 0;
}

/* 模块卸载时释放数据缓冲;NIF 的 unload 回调在个别场景可能不被调用,
 * 库按单例常驻设计,这里仅为尽力回收。 */
static void
unload(ErlNifEnv *env, void *priv) {
    (void)env;
    (void)priv;
    if (p_ipdb != NULL) {
        free((void *)p_ipdb->buffer);
        free(p_ipdb);
        p_ipdb = NULL;
    }
}

static ErlNifFunc
nif_funcs[] = {
    {"lookup_nif", 1, lookup, 0}
};

ERL_NIF_INIT(eqqwry, nif_funcs, &load, NULL, NULL, &unload)
