# AGENTS.md — eqqwry 项目协作指令

纯真 QQWry.Dat 的 Erlang NIF 解析库。Erlang 侧入口 `src/eqqwry.erl`,C 侧 NIF
`c_src/eqqwry.c`,数据文件 `priv/qqwry.dat`(纯真官方 dat 格式,GBK 编码)。

## 常用命令

```sh
rebar3 compile     # 编译 Erlang + C(port compiler)
rebar3 eunit       # 运行 test/ 下 eunit 测试
rebar3 clean       # 清理 _build 与产物
```

## 构建机制要点

- `rebar.config` 使用 **pc 插件**(`{plugins, [pc]}` + `provider_hooks`)。
  rebar3 3.14+ 不再内置 port compiler,改动时勿移除这两段。
- pc 按 `{artifacts, ["priv/eqqwry.so"]}` 判断是否需要重建:**产物已存在时
  会跳过 C 编译**。改了 `c_src/` 却没看到编译输出时,先 `rm -f priv/eqqwry.so`
  或 `rebar3 clean` 再编译。
- C 编译开启 `-Wall -Wextra`,提交前保持 0 警告。

## 数据更新流程

1. 从 [nmgliangwei/qqwry releases](https://github.com/nmgliangwei/qqwry/releases)
   下载最新官方 dat(简体版),核对 release 页 sha256。
2. 覆盖 `priv/qqwry.dat`,运行 `rebar3 eunit`。
3. 测试中的期望文案(`test/eqqwry_tests.erl`)与数据版本联动,归属地文案变化时同步更新。
4. 数据格式本身(索引区/记录区/重定向语义)不变,`eqqwry.c` 与码表无需改动。
   仅当官方出现新的编码形态时才重新生成码表:
   `python3 scripts/gen_gbk_table.py > c_src/gbk_table.h`。

## 数据格式与边界语义(实现须与之一致)

- 文件头两个小端 uint32 为索引区首/末偏移;索引条目 7 字节:起始 IP(4B)+
  记录偏移(3B);记录头 4 字节为结束 IP;字符串为 GBK。
- 记录解析:`0x01` 重定向到实际字符串位置(正常单层,实现最多跟随 8 层作损坏
  防御);`0x02` 表示字符串位于共享区,否则为内联 NUL 结尾字符串。
- 二分查找取"起始 IP ≤ 目标"的最大索引;`ip < lower[0]` 或 `ip > upper` 返回
  `not_found`(区间近似全覆盖,0.0.0.0 除外)。尾部允许重叠(后段细化)。
- 3 字节偏移上限 16MB:记录区+字符串区必须位于文件前 16,777,216 字节内,
  索引区可以放其后。**不要引入私有格式变体**。

## 代码约定

- 代码注释一律使用中文。
- C 侧:所有整数字段**逐字节小端组装**(`get_u32le`/`get_u24le`),禁止未对齐
  指针读取(可移植性);解析前对所有偏移做文件边界校验,损坏数据安全返回
  `not_found`,不允许越界读或死循环。
- GBK→UTF-8 转换的**输出长度必须按码点精确计算**(`utf8_len` 与 `ucs_to_utf8`
  保持一致):GBK 中有 157 个码位映射到 U+0080~U+07FF(UTF-8 仅 2 字节),
  若按"双字节恒占 3 字节"估算,返回值尾部会残留未初始化字节。
- Erlang 侧 API:`lookup/1` 返回 `{StartIP, EndIP, Zone, Area}`,Zone/Area 为
  UTF-8 binary;`lookup/1` 在 Erlang 层校验四个字节均为 0~255 的整数,
  非法入参统一报 `badarg`(NIF 侧仍对超出 uint32 的值报 `badarg`)。
- NIF 数据为进程无关只读单例(`p_ipdb`),查询路径不加锁。

## 验证方法

改完 `c_src/` 后除了跑 eunit,建议做随机对拍:用 Python 参考实现(标准 qqwry
解析逻辑)对若干随机 IP 生成期望值,与 NIF 结果逐一比对(最近一次验证 23180 个
随机/索引抽样 IP 全部一致,另单独校验 1812 条含 U+00B7 的记录;脚本思路见
README"项目结构"注记)。
