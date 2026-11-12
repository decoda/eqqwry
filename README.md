# eqqwry

纯真数据库 QQWry.Dat 的 Erlang NIF 解析版。

- 数据:纯真官方 dat 格式(标准格式,未做任何私有扩展),查询时由 NIF 内嵌的
  GBK→UTF-8 码表(`c_src/gbk_table.h`)完成编码转换,`lookup/1` 直接返回 UTF-8 binary。
- 性能:内存二分查找 + 字符串转换,单次查询约 2 微秒。
- 边界语义:IP 低于库内最小 IP(`0.0.0.1`)或落在区间空洞时返回 `not_found`。

## 用法

```erlang
eqqwry:lookup({114,247,50,2}).
%% => {{114,247,45,133},{114,249,20,255},
%%     <<"中国–北京–北京"/utf8>>, <<"联通"/utf8>>}

eqqwry:lookup({0,0,0,0}).
%% => not_found
```

返回值:`{StartIP, EndIP, Zone, Area}`,Zone/Area 为 UTF-8 编码的 binary。
`lookup/1` 要求四个字节均为 0~255 的整数,非法入参(如 `{1,300,0,0}`)报 `badarg`。

## 构建

需要 rebar3(rebar3 3.14+ 移除了内置 pc 插件,本仓库已显式声明):

```sh
rebar3 compile
rebar3 eunit    # 运行测试
```

## 更新数据文件

数据文件 `priv/qqwry.dat` 为纯真官方 dat 格式,更新时直接用最新官方 dat
覆盖该文件即可(NIF 与码表无需任何改动)。每日同步的官方格式 dat 可从
[nmgliangwei/qqwry](https://github.com/nmgliangwei/qqwry/releases) 获取,
下载后请核对 release 页面给出的 sha256。

当前打包版本:2026-11-11(对应 sha256
`1b132689cc734a0cb7f709775ea42db6cf82c2b37d2df1343badb79ef951c089`)。

## 项目结构

```
src/eqqwry.erl          Erlang 入口(on_load 加载 NIF、IP 与整数互转)
c_src/eqqwry.c          NIF 实现:二分查找 + GBK→UTF-8 转换
c_src/gbk_table.h       GBK→Unicode 码表(由 scripts/gen_gbk_table.py 生成)
scripts/gen_gbk_table.py 码表生成脚本(仅在编码规则变化时需重新生成)
test/eqqwry_tests.erl   eunit 冒烟测试(期望值与数据版本联动)
priv/qqwry.dat          纯真官方 dat 数据文件
```

编码转换参考:[shuax/LocateIP](https://github.com/shuax/LocateIP)
