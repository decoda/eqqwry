-module(eqqwry).

%% eqqwry: eqqwry library's entry point.

-export([lookup/1]).
-on_load(init/0).


%% API

%% init/0 在模块加载时把 dat 文件路径传给 NIF。
%% NIF 路径与数据文件均位于 priv/ 下(ebin/../priv)。
init() ->
    case code:which(?MODULE) of
        Filename when is_list(Filename) ->
            Path = filename:dirname(Filename),
            Nif = filename:join([Path, "../priv", "eqqwry"]),
            Dat = filename:join([Path, "../priv", "qqwry.dat"]),
            erlang:load_nif(Nif, Dat)
    end.

%% lookup({A,B,C,D}) ->
%%   {StartIP, EndIP, Zone, Area};
%%   not_found.
%% StartIP/EndIP 为该 IP 段边界(4 元组);
%% Zone/Area 为 UTF-8 编码的 binary(NIF 内部完成 GBK 转换)。
%% 四个字节必须均为 0~255 的整数,否则报 badarg。
lookup({A, B, C, D})
  when is_integer(A), A >= 0, A =< 255,
       is_integer(B), B >= 0, B =< 255,
       is_integer(C), C >= 0, C =< 255,
       is_integer(D), D >= 0, D =< 255 ->
    case lookup_nif(ip2int({A, B, C, D})) of
        {Lower, Upper, Zone, Area} ->
            {int2ip(Lower), int2ip(Upper), Zone, Area};
        Other ->
            Other
    end;
lookup(_Other) ->
    erlang:error(badarg).

%% Internals

lookup_nif(_) ->
    erlang:nif_error({error, not_loaded}).

ip2int({A,B,C,D}) -> (A bsl 24) bor (B bsl 16) bor (C bsl 8) bor D.
int2ip(IP) -> {(IP bsr 24) band 16#FF, (IP bsr 16) band 16#FF, (IP bsr 8) band 16#FF, IP band 16#FF}.

%% End of Module.
