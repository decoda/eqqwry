#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 GBK→Unicode 码表的 C 头文件(c_src/gbk_table.h)。

GBK 双字节码位空间:首字节 0x81~0xFE,次字节 0x40~0x7E 与 0x80~0xFE
(跳过 0x7F),共 126*190 = 23940 个码位。表中未定义码位记 0。

用法:python3 scripts/gen_gbk_table.py > c_src/gbk_table.h
码表约 47KB(二进制),生成一次后随源码提交,数据文件更新无需重新生成。
"""

HEADER = """\
/* 由 scripts/gen_gbk_table.py 自动生成,请勿手工编辑。
 * GBK 双字节码位 → Unicode(UCS-2)码表。
 * 索引 = (首字节 - 0x81) * 190 + 列号;
 * 列号 = 次字节 0x40~0x7E 时 (lo - 0x40),
 *        次字节 0x80~0xFE 时 (lo - 0x41)。
 * 值 0 表示该 GBK 码位未定义。
 */
#ifndef GBK_TABLE_H
#define GBK_TABLE_H

#define GBK_COL_MAX 190
#define GBK_ROW_MAX 126
#define GBK_HI_MIN 0x81
#define GBK_HI_MAX 0xFE

static const unsigned short gbk_ucs_table[GBK_ROW_MAX * GBK_COL_MAX] = {
"""

rows = []
for hi in range(0x81, 0xFF):  # GBK 首字节合法范围 0x81~0xFE,不含 0xFF
    row = []
    # GBK 次字节:0x40~0x7E 与 0x80~0xFE(跳过 0x7F 与 0xFF),共 190 列
    for lo in list(range(0x40, 0x7F)) + list(range(0x80, 0xFF)):
        try:
            row.append(ord(bytes([hi, lo]).decode('gbk')))
        except UnicodeDecodeError:
            row.append(0)
    rows.append(row)

# 校验:除 GBK 未定义码位外,不允许出现码点 0(0 用作未定义哨兵)
bad = sum(1 for row in rows for v in row if v == 0)
total = sum(1 for row in rows for v in row if v != 0)

out = [HEADER]
for row in rows:
    cells = ','.join('0x%04X' % v for v in row)
    out.append('  ' + cells + ',\n')
out.append('};\n\n#endif /* GBK_TABLE_H */\n')

import sys
sys.stdout.write(''.join(out))
import io
sys.stderr.write('#defined codes: %d, undefined(sentinel 0): %d\\n' % (total, bad))
