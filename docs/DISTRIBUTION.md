# Distribution

The Bodycam build ships as three packages, the same split PiP uses: the engine repo carries only
gamedata that works on plain Anomaly, and every file that replaces another mod's file ships in a
separate MO2 mod.

| Package | Repo | Install |
| --- | --- | --- |
| Core: engine and core gamedata | this repo | `bin` into `Anomaly\bin`, the db0 into `Anomaly\db\mods` |
| Bodycam GAMMA Patch (required on G.A.M.M.A.) | `asuparabekon/xray-monolith-bodycam-gamma-compatibility-patch` | MO2 mod; the conflicts it must win are in its README |
| Bodycam Grass Optimisation (optional) | `asuparabekon/xray-monolith-bodycam-grass-optimisation-patch` | MO2 mod above SSS 23, KennShade and Grass Tweaks |

The core zip holds `bin\` (exe, pdb) and `db\mods\00_modded_exes_gamedata.db0`. Each mod zip holds
`meta.ini`, `README.md`, `PROVENANCE.md` and `gamedata/`, named `<mod name> <version>.zip`.

No gamedata path ships in two packages, with two exceptions:

- core files a mod overrides on purpose: `night_vision.h` in the Bodycam GAMMA Patch and the two
  `deffer_tree_branch_*.vs` stubs in the grass mod;
- core mirrors: core scripts and texts the Bodycam GAMMA Patch ships as exact copies, because a
  G.A.M.M.A. install keeps older Monolith copies of them loose in `Anomaly\gamedata`, which outrank the
  core db0. The mirrors must match the core files of the same engine version, line endings aside.

## What the core may contain

Core shaders must resolve their includes from this repo and the base Anomaly archive alone. The only
exceptions are passes the engine creates when another mod is installed:

- **3DSS-gated:** the PiP glue (`scope_depth_write`, `svp_taa_stamp`, `svp_distort_stamp`,
  `svp_lens_stencil`, `scope_vertex`, `scope_custom_depth.h`). `EnsureScopeShaders`
  (`svp_optics.cpp`) creates it only when the 3D Shader Scopes framework files exist
  (`scope_3dss_common.h`, `scope_common.h`, `scope_color_write.ps`); on plain Anomaly it logs that
  the framework is not installed and stays empty. The Reference reticle, glyph, mesh and tonemap
  passes run only on a captured 3DSS scope lens.
- **SSS-gated:** the Reference fog pass, which runs inside the SSS fog scattering phase.

Everything that patches 3DSS, SSS, Beef's NVG, Boomsticks or GAMMA scripts, and the GAMMA weapon rows
for PiP's data tables (DLTX `mod_pip_*_gamma_patch.ltx`), belongs in the Bodycam GAMMA Patch.
