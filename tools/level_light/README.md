# Baked level lighting: find and fix pitch-black surfaces

A static surface renders black when the hemi occlusion baked into the level is zero: `hmodel()`
multiplies the entire ambient term by it, so zero means no ambient at all, and only a dynamic light
(the flashlight) shows the texture. It is baked data, so it looks the same in vanilla — the fix is to
patch the level's own files, which needs no recompile: a loose file under
`gamedata\levels\<level>\` overrides the archived one, and deleting it reverts.

Levels come unpacked with the game at `<game>\levels\gamedata\levels\<level>\` — that folder is the
input to every tool here.

## Where the number lives

| surface family (from the shader string in `level` chunk 2) | hemi is |
|---|---|
| `default/<tex>,lmap#N_1,lmap#N_2` | ALPHA of `lmap#N_2.dds` under the surface's lightmap UVs |
| `def_shaders\def_vertex/<tex>` and friends | ALPHA of the packed normal in each vertex, in `level.geom` |

`s_hemi` is `L_textures[2]` (the third texture), and the vertex shader passes the normal's `.w`
through as `O.position.w` — see `deffer_base_flat.vs` / `.ps`.

## Workflow

1. In game, aim at the black spot and run `look_at` (console). It prints the world point — compiled
   levels keep no object names, so a position is the only address a surface has.
2. `lvl_pick.py <level dir> X Y Z [...]` — the nearest triangle to each point, and what the shader
   reads *there*. Do NOT identify by bbox: a merged batch's box spans half a building and will match
   both the broken surface and a healthy one next to it.
3. Patch:
   * vertex-lit: `geom_patch.py <level dir> <out dir> <visual ids> --floor 20`
   * lightmapped: `lmap_patch.py <level dir> <out dir> <visual ids> --hemi 45`
   `<out dir>` is `<game>\gamedata\levels\<level>\`. Both write the minimum: geom_patch touches only
   the hemi bytes, lmap_patch re-encodes only the DXT5 blocks the painted texels fall in.

## Surveying

* `lvl_scan.py <level dir> [--tex X] [--near X Y Z R] [--top N]` — surfaces ranked by the hemi they
  actually sample.
* `lvl_dark.py` / `cluster_hi.py` — near-black surfaces grouped into spatial clusters. Interiors show
  up here legitimately (a bunker really has no sky), so read the list with that in mind.
* `lvl_at.py`, `lvl_lmap.py`, `lmap_probe.py`, `geom_stats.py` — bbox lookup, a visual's lightmap UV
  island with an alpha crop, both lightmap channels (`.a` hemi, `.g` static sun), vertex histogram.

## Two traps, both paid for once

* Measuring the vertex hemi of a *lightmapped* surface is meaningless — the shader never reads it,
  and it is zero for most of them. Pick the family from the shader string.
* Sampling a lightmap *at the vertices* reads the island's padding, which is black, so healthy
  surfaces look broken. Sample triangle centroids (which needs the index buffer).
