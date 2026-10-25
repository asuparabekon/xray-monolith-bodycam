#include "stdafx.h"
#include "../../xrEngine/xr_ioconsole.h"
#include "../../xrEngine/xr_ioc_cmd.h"
#include "svp_report.h"

void svp_report_configuration()
{
	// pip one-shot config fingerprint requested through r__svp_report
	{
		if (ps_r__svp_report)
		{
			ps_r__svp_report = 0;
			// build fingerprint header first, testers diff their log against this
			PipMsg("[SVP-CFG] build %s mode=%d", __DATE__, scope_svp_enabled);
			// generate the cvar list from the console registry so it never drifts from the registered
			// set, every r__svp_/g_svp_/s3ds_ command with its live value grouped about eight per line
			if (Console)
			{
				char line[1024];
				line[0] = 0;
				u32 grp = 0;
				for (const auto& c : Console->Commands)
				{
					const char* nm = c.second->Name();
					if (!(0 == strncmp(nm, "r__svp_", 7) || 0 == strncmp(nm, "g_svp_", 6) || 0 == strncmp(nm, "s3ds_", 5)))
						continue;
					IConsole_Command::TStatus st;
					st[0] = 0;
					c.second->Status(st);
					char tok[288];
					xr_sprintf(tok, "%s%s=%s", grp ? " " : "", nm, st);
					// xr_strcat aborts on overflow, flush early when long values would not fit
					if (xr_strlen(line) + xr_strlen(tok) >= sizeof(line))
					{
						PipMsg("[SVP-CFG] %s", line);
						line[0] = 0;
						grp = 0;
						xr_sprintf(tok, "%s=%s", nm, st);
					}
					xr_strcat(line, tok);
					if (++grp == 8)
					{
						PipMsg("[SVP-CFG] %s", line);
						line[0] = 0;
						grp = 0;
					}
				}
				if (line[0])
					PipMsg("[SVP-CFG] %s", line);
				// aa state read the same registry way, ssfx_taa is the script-driven taa control
				IConsole_Command::TStatus a_taa, a_aa, a_ker;
				a_taa[0] = a_aa[0] = a_ker[0] = 0;
				auto st_of = [](const char* name, IConsole_Command::TStatus& out)
				{
					auto it = Console->Commands.find(name);
					if (it != Console->Commands.end())
						it->second->Status(out);
				};
				st_of("ssfx_taa", a_taa);
				st_of("r2_aa", a_aa);
				st_of("r2_aa_kernel", a_ker);
				PipMsg("[SVP-CFG] aa ssfx_taa=%s r2_aa=%s r2_aa_kernel=%s", a_taa, a_aa, a_ker);
				// present pipeline isn't an r__svp_/g_svp_/s3ds_ cvar so the walk above never prints it
				extern int ps_framelimiter;
				PipMsg("[SVP-CFG] swapchain_buffers=%d present_interval=%d framelimit=%d", ps_r__swapchain_buffers,
					psDeviceFlags.test(rsVSync) ? 1 : 0, ps_framelimiter);
			}

			// pip [SVP-FILES] shader source truth: where each scope file resolves from (loose or which
			// archive), size and source crc, plus the precompiled-blob and mounted-archive taints
			{
				static const char* s_scope_files[] = { "scope_color_write.ps", "scope_3dss_common.h", "scope_common.h",
					"scope_custom_image.h", "scope_custom_reticle.h", "scope_custom_shadow.h", "scope_custom_lens.h", "mark_adjust.h",
					"svp_hooks_common.h", "svp_hooks_image.h", "svp_hooks_reticle.h", "svp_hooks_lens.h", "svp_hooks_shadow.h",
					"models_scope_reticle.ps", "models_scope_reticle_precise.ps", "models_reflex_reticle.ps",
					"models_reflex_reticle_3db.ps", "models_reflex_reticle_simple.ps", "models_reflex_reticle_simple_3db.ps",
					"models_reflex_lens.ps", "models_scope_zwrite.ps", "models_scope_reticle.s", "models_scope_reticle_precise.s",
					"models_reflex_reticle.s", "models_reflex_reticle_3db.s", "models_reflex_reticle_simple.s",
					"models_reflex_reticle_simple_3db.s", "models_reflex_lens.s", "models_scope_zwrite.s", "models_scope_back.s",
					"models_scope_reticle.vs", "models_reflex_reticle.vs", "models_reflex_lens.vs", "scope_vertex.vs", "scope_defines.h",
					"svp_nearblur.ps", "scope_depth_write.ps", "svp_taa_stamp.ps", "gbuffer_stage.h", "nv_utils.h", "thermal_utils.h",
					"night_vision.h", "nightvision_gen_1.ps", "nightvision_gen_2.ps", "nightvision_gen_3.ps", "combine_1.ps",
					"combine_2_naa.ps", "pp_blur.ps", "sky2.ps" };
				for (const char* fn : s_scope_files)
				{
					string_path rel;
					strconcat(sizeof(rel), rel, "r3\\", fn);
					const CLocatorAPI::file* f = FS.exist("$game_shaders$", rel);
					if (!f)
					{
						PipMsg("[SVP-FILES] %-32s MISSING", fn);
						continue;
					}
					u32 crc = 0;
					char ver[96] = "unmarked";
					if (IReader* r = FS.r_open("$game_shaders$", rel))
					{
						crc = crc32(r->pointer(), r->length());
						// the resolved source carries our PIP_COMPAT_PATCH tag, a foreign shader that
						// shadowed ours or a stale pre-marker file reads unmarked
						const char* p = (const char*)r->pointer();
						const u32 n = r->length();
						const char* key = "PIP_COMPAT_PATCH";
						const u32 klen = (u32)xr_strlen(key);
						for (u32 i = 0; n > klen && i <= n - klen; ++i)
							if (0 == strncmp(p + i, key, klen))
							{
								u32 j = i + klen, k = 0;
								while (j < n && p[j] != '\r' && p[j] != '\n' && k < sizeof(ver) - 1)
									ver[k++] = p[j++];
								ver[k] = 0;
								break;
							}
						FS.r_close(r);
					}
					const char* src = "loose";
					if (f->vfs != 0xffffffff && f->vfs < FS.m_archives.size())
						src = FS.m_archives[f->vfs].path.c_str();
					PipMsg("[SVP-FILES] %-32s %6u bytes crc %08x ver[%s] %s", fn, f->size_real, crc, ver, src);
				}
				// shipped precompiled blobs load with no source crc and shadow every source edit
				FS_FileSet blobs;
				string_path bdir;
				FS.update_path(bdir, "$game_shaders$", "r3\\objects\\r4\\scope_color_write.ps\\");
				FS.file_list(blobs, bdir, FS_ListFiles | FS_RootOnly, "*");
				PipMsg("[SVP-FILES] dispatcher precompiled blobs %u, rs_precompiled_shaders %d", (u32)blobs.size(),
					psDeviceFlags2.test(rsPrecompiledShaders) ? 1 : 0);
				u32 n_arch = 0;
				for (const auto& A : FS.m_archives)
					if (strstr(A.path.c_str(), "mods"))
					{
						PipMsg("[SVP-FILES] mounted mods archive %s", A.path.c_str());
						++n_arch;
					}
				PipMsg("[SVP-FILES] mods archives mounted %u", n_arch);
			}
		}
	}
}
