%% eqqwry 的 eunit 冒烟测试。
%% 注意:测试依赖 priv/qqwry.dat 数据文件的具体内容,
%% 若官方数据更新导致文案变化,期望值需同步调整。
-module(eqqwry_tests).

-include_lib("eunit/include/eunit.hrl").

%% 已知 IP 的归属地(2026-11 CZDB 数据)
lookup_known_ip_test() ->
    {S, E, Zone, Area} = eqqwry:lookup({1, 1, 1, 1}),
    ?assertEqual({1, 1, 1, 1}, S),
    ?assertEqual({1, 1, 1, 1}, E),
    ?assertEqual(<<"澳大利亚"/utf8>>, Zone),
    ?assertEqual(<<"APNIC/CloudFlare公共DNS服务器"/utf8>>, Area).

lookup_domestic_ip_test() ->
    {_S, _E, Zone, Area} = eqqwry:lookup({114, 247, 50, 2}),
    ?assertEqual(<<"中国–北京–北京"/utf8>>, Zone),
    ?assertEqual(<<"联通"/utf8>>, Area).

lookup_lan_ip_test() ->
    {_S, _E, Zone, Area} = eqqwry:lookup({192, 168, 1, 1}),
    ?assertEqual(<<"IANA">>, Zone),
    ?assertEqual(<<"局域网IP(Private-Use)"/utf8>>, Area).

%% 段边界一致:返回值两端应构成合法 IP 元组
lookup_range_shape_test() ->
    {S, E, Zone, Area} = eqqwry:lookup({8, 8, 8, 8}),
    ?assert(is_tuple(S) andalso tuple_size(S) =:= 4),
    ?assert(is_tuple(E) andalso tuple_size(E) =:= 4),
    ?assert(S =< {8, 8, 8, 8} andalso {8, 8, 8, 8} =< E),
    ?assert(is_binary(Zone) andalso is_binary(Area)).

%% 低于库内最小 IP(0.0.0.1)应返回 not_found
lookup_below_min_test() ->
    ?assertEqual(not_found, eqqwry:lookup({0, 0, 0, 0})).

%% 库内最大段(纯真网络版本信息)应可查询
lookup_top_range_test() ->
    {_S, E, Zone, Area} = eqqwry:lookup({255, 255, 255, 255}),
    ?assertEqual({255, 255, 255, 255}, E),
    ?assert(is_binary(Zone) andalso is_binary(Area)).

%% 超范围参数应报 badarg(每个字节都必须是 0~255 的整数)
lookup_bad_arg_test() ->
    ?assertError(badarg, eqqwry:lookup({256, 0, 0, 0})),
    ?assertError(badarg, eqqwry:lookup({1, 300, 0, 0})),
    ?assertError(badarg, eqqwry:lookup({1, 2, 3, 256})),
    ?assertError(badarg, eqqwry:lookup({0, 0, 0, -1})),
    ?assertError(badarg, eqqwry:lookup({1, 2, 3})),
    ?assertError(badarg, eqqwry:lookup({1, 2, 3, 4, 5})).

%% 返回的 Zone/Area 必须是合法 UTF-8,且字节数与字符编码长度完全一致
%% (若转换时按"双字节字符恒占 3 字节"估算长度,尾部会残留未初始化字节)
utf8_validity_test() ->
    Ips = [{1, 1, 1, 1}, {114, 247, 50, 2}, {8, 8, 8, 8}, {139, 75, 0, 0}],
    lists:foreach(fun(IP) ->
        {_S, _E, Zone, Area} = eqqwry:lookup(IP),
        exact_utf8(Zone),
        exact_utf8(Area)
    end, Ips).

%% 回归:GBK 0xA1A4 → U+00B7('·'),UTF-8 仅 2 字节。
%% 含该字符的记录(如 "Old·Dominion大学")在旧实现里会多出一个尾部垃圾字节。
interpunct_record_test() ->
    {_S, _E, _Zone, Area} = eqqwry:lookup({139, 75, 0, 0}),
    ?assertEqual(<<"阿尔弗里德·韦格纳极地与海洋研究所"/utf8>>, Area),
    ?assertEqual(byte_size(Area), utf8_size(unicode:characters_to_list(Area))).

%% ---- 辅助函数 ----

%% 校验 binary 是合法 UTF-8 且无多余字节,原样返回
exact_utf8(Bin) when is_binary(Bin) ->
    Chars = unicode:characters_to_list(Bin),
    ?assert(is_list(Chars)),
    ?assertEqual(byte_size(Bin), utf8_size(Chars)),
    Bin.

%% 字符列表按 UTF-8 编码后的字节数
utf8_size(Chars) ->
    lists:sum([case C of
                 C when C < 16#80 -> 1;
                 C when C < 16#800 -> 2;
                 _ -> 3
             end || C <- Chars]).
