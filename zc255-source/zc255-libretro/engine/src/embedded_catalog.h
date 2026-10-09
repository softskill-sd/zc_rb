#pragma once
#include <stddef.h>
namespace zc255_assets {
struct Entry { const char* path; unsigned resource_id; size_t size; };
inline constexpr Entry entries[] = {
{"allegro5.cfg",30000u,7900u},
{"assets/190_tiles.qst",30001u,162657u},
{"assets/cursor.bmp",30002u,1334u},
{"assets/dungeon.mid",30003u,2506u},
{"assets/ending.mid",30004u,12073u},
{"assets/gameover.mid",30005u,685u},
{"assets/gui_pal.bmp",30006u,1334u},
{"assets/level9.mid",30007u,3357u},
{"assets/overworld.mid",30008u,8404u},
{"assets/title.mid",30009u,8601u},
{"assets/triforce.mid",30010u,658u},
{"base_config/zc.cfg",30011u,3593u},
{"base_config/zscript.cfg",30012u,242u},
{"Classic.nsf",30013u,61568u},
{"default.sf2",30014u,32319396u},
{"gamecontrollerdb.txt",30015u,610778u},
{"modules/classic/classic_fonts.dat",30016u,102765u},
{"modules/classic.zmod",30017u,14616u},
{"sfx.dat",30018u,1249133u},
};
inline constexpr char identity[] = "73cf3f7a0aece40dc9464eb235aa5e77878719090982bcd2b4358edd9ed67f0d";
inline constexpr const char* sha256s[] = {
"0708b3a1a70ab3fdb946dac020553327ba1af9e141604055470f93dd0cee446f",
"d34518db9d9dfbdcabbb17d91d17f410db501a30110f79c7e675cf99f7f7fdfc",
"cc0cf14589b378f2cb1bc941e500d9c2a8c478f3c41c25af95842b1ab1b20c47",
"5a01be35680baa8643fa908e32eb20636eeaed6c62e5d6f96f5274d8b870a509",
"cabe8b03fb65decb6323dd4ffa112b577f9e8ac7e6d6fd3e6b234bd136f996e5",
"0f03243ecbad9f475a90d476bddef6a8c49676960510597a4c68a5e89551812c",
"20f25c342305b9d948974769cade7795a91d57e845d1e69c078f26cb28fec15a",
"4bacdc890f4cb1329ec4250fe7b4e2b3121a2a51f5e7509750c60b8befdd3d5b",
"2f5e2b79b95efe969d9af8b7c2986b6e982695f5490186519221377ad0babac3",
"57b2505f0d172da54da29054265f1e7096d7b8e056dcdcdbb7625ff8081a1347",
"9b1f6abea67be5e5efdce2b1ada85a66df041e67528a70d9e0532117b4ffdec5",
"4be1e7d9742ed291b4fdd56fc562bacbab68b3f97d38555332b1013ff49c6602",
"83682473c9d9cccecf419634da7622594f3c10d84b402204842c04e449a49cce",
"cf6256c64327a3edeb259fbc6ac09102273bbbbf0f0cff26dbbd1a256ce12e3e",
"9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe",
"fc61fec34b1c431596aa9701ace5d8674d0e93137c52d2dfe508386efe1ee719",
"e1008c6ea910a67071ef5825a9dfe5324fbacd15af2e050d0a5325e13f880958",
"51338b57d227d4bb44aadfcf8d71bccf6c3f8e893b55f50c2cd6554e895911b1",
"ddd7595fb1320cf758896d75e48cef14e331336601e215a99d54427d741040f7",
};
inline constexpr size_t entry_count=sizeof(entries)/sizeof(entries[0]);
inline constexpr size_t total_bytes=34571600u;
}
