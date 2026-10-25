# Source provenance

This v0.3.1 source snapshot combines the Bodycam fork with upstream engine and PiP work.
Squashing the development history does not change ownership of the included code or assets.

| Component | Source / checkpoint |
| --- | --- |
| Published Bodycam baseline | `asuparabekon/xray-monolith-bodycam` v0.3, commit `65faa2f04dc3d83404650a24833d815f81a42f9b` |
| Development snapshot | Full-history Bodycam checkpoint `a24d472a0`, before file-selection cleanup |
| Monolith MT integration | `themrdemonized/xray-monolith`, merged through `f768576c0` |
| PiP integration | `xray-monolith-pip`, integrated through `406e7c015`; original PiP work credited to TheLostInPlace (m22) |
| Grass pipeline | TheLostInPlace's fps-clean grass work, ported onto the Bodycam scope pass |
| Vector reticles | pugixml 1.14 and stb_image 2.30 under `src/3rd party/`; see THIRD-PARTY-NOTICES.txt |
| Reference motion and optics | Accepted reference checkpoint `2b72576a7c8b4c0d6c2fb28f4f7483373ccb66ad`; original recording/export inputs are maintained separately |
| Optick submodule | `bombomby/optick`, pinned by the Git submodule entry and `.gitmodules` |

## Included assets and notices

Reference character packets are included under `gamedata/bodycam_reference/character/`.
Optics constants are under `gamedata/bodycam_reference/optics/`.
Generated source headers retain their source/generator notices. Historical paths in provenance
records identify the original inputs; they are not installation requirements.

The root [License.txt](../License.txt), [THIRD-PARTY-NOTICES.txt](../THIRD-PARTY-NOTICES.txt),
component licenses and source-level notices remain part of the distribution. This summary does
not replace those notices or grant additional rights to third-party material.

## Separate projects

- [GAMMA compatibility patch](https://github.com/asuparabekon/xray-monolith-bodycam-gamma-compatibility-patch):
  mod-specific shaders, scripts and reticle assets; its `PROVENANCE.md` records their sources.
- [Grass Optimisation patch](https://github.com/asuparabekon/xray-monolith-bodycam-grass-optimisation-patch):
  optional grass shaders and their upstream attribution.
- [3DSS Reticle Studio](https://github.com/asuparabekon/3dss-reticle-studio): separate reticle-authoring application.

Development handoffs, investigations, fixtures and tools are kept outside this public snapshot.
