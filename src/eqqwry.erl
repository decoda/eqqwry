-module(eqqwry).

%% eqqwry: eqqwry library's entry point.

-export([lookup/1]).
-on_load(init/0).


%% API

init() ->
    case code:which(?MODULE) of
        Filename when is_list(Filename) ->
            Path = filename:dirname(Filename),
            Nif = filename:join([Path, "../priv", "eqqwry"]),
            Dat = filename:join([Path, "../priv", "qqwry-utf8.dat"]),
            erlang:load_nif(Nif, Dat)
    end.

lookup({A,B,C,D}) ->
    case lookup_nif(ip2int({A,B,C,D})) of
        {Lower, Upper, Zone, Area} ->
            {int2ip(Lower), int2ip(Upper), Zone, Area};
        Other ->
            Other
    end.

%% Internals

lookup_nif(_) ->
    erlang:nif_error({error, not_loaded}).

ip2int({A,B,C,D}) -> (A bsl 24) bor (B bsl 16) bor (C bsl 8) bor D.
int2ip(IP) -> {(IP bsr 24) band 16#FF, (IP bsr 16) band 16#FF, (IP bsr 8) band 16#FF, IP band 16#Ff}.

%% End of Module.
