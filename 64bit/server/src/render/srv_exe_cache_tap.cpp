// srv_exe_cache_tap.cpp - the EXE-cache tap as a TileCacheSource (issue 3301). Origin: srv_exe_cache_tap.h (ported from
// shell/server/shell_server.cpp BakePoll at commit b4f7748; that file is unchanged).
#include "srv_exe_cache_tap.h"

namespace srvrender {

namespace c5t = c5r::terrain;

namespace {
const char* kBakeStateNames[] = {"NONE", "ACTIVE", "OFF", "FAILED"};

bool MapNamedRead(const wchar_t* name, HANDLE& h, const uint8_t*& view, size_t& size)
{
	h = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
	if(!h)
		return false;
	view = (const uint8_t*)MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0);
	if(!view)
	{
		CloseHandle(h);
		h = nullptr;
		return false;
	}
	MEMORY_BASIC_INFORMATION mbi = {};
	size = VirtualQuery(view, &mbi, sizeof(mbi)) ? mbi.RegionSize : 0;
	return true;
}

// The typed view format of a typeless texture format (the formats the EXE's caches and the stand-in use); a typed format
// is returned unchanged; UNKNOWN for a typeless format without a mapping here (shell_server.cpp TypedViewFormat).
DXGI_FORMAT TypedViewFormat(DXGI_FORMAT f)
{
	switch(f)
	{
	case DXGI_FORMAT_BC1_TYPELESS: return DXGI_FORMAT_BC1_UNORM;
	case DXGI_FORMAT_BC2_TYPELESS: return DXGI_FORMAT_BC2_UNORM;
	case DXGI_FORMAT_BC3_TYPELESS: return DXGI_FORMAT_BC3_UNORM;
	case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
	case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
	case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_UNORM;
	case DXGI_FORMAT_BC4_TYPELESS:
	case DXGI_FORMAT_BC5_TYPELESS:
	case DXGI_FORMAT_BC7_TYPELESS:
	case DXGI_FORMAT_R32G32B32A32_TYPELESS:
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_R8_TYPELESS:
	case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_UNKNOWN;
	default: return f;
	}
}
}  // namespace

void ExeCacheTap::onField(ID3D11Device* dev, const c5r::heightfield::HeightGrid& grid, uint32_t tilePlots, uint32_t loadSeq)
{
	(void)grid;
	(void)loadSeq;
	if(dev != m_dev)
	{
		SafeRelease(m_dev1);
		m_dev = dev;
		if(dev)
			dev->QueryInterface(__uuidof(ID3D11Device1), (void**)&m_dev1);
	}
	m_haveField = true;
	m_fieldTilePlots = tilePlots;
	for(auto& sl : m_slots)
		sl.applied = false;   // setHeightField dropped every cache
	m_manApplied = false;
}

void ExeCacheTap::onPlotsChanged(const uint32_t* plotIndices, size_t count)
{
	(void)plotIndices;
	plotsChanged += (int64_t)count;   // the EXE re-bakes and the tap follows; counted only
}

void ExeCacheTap::beforeDraw(ID3D11DeviceContext* ctx, c5t::Renderer& r, const ViewInfo& view, double budgetMs)
{
	(void)ctx;
	(void)view;
	(void)budgetMs;
	poll(r);
}

void ExeCacheTap::idle(ID3D11DeviceContext* ctx, double budgetMs)
{
	(void)ctx;
	(void)budgetMs;
}

void ExeCacheTap::releaseSlots()
{
	for(auto& sl : m_slots)
		for(int k = 0; k < 2; ++k)
		{
			SafeRelease(sl.srv[k]);
			SafeRelease(sl.tex[k]);
		}
	m_slots.clear();
}

void ExeCacheTap::unmapManifest()
{
	if(m_manView)
	{
		UnmapViewOfFile(m_manView);
		m_manView = nullptr;
	}
	if(m_manSec)
	{
		CloseHandle(m_manSec);
		m_manSec = nullptr;
	}
	m_manSize = 0;
}

void ExeCacheTap::release()
{
	releaseSlots();
	unmapManifest();
	m_manName[0] = 0;
	m_manApplied = false;
	m_haveField = false;
	SafeRelease(m_dev1);
	m_dev = nullptr;
}

void ExeCacheTap::statsLine(char* out, size_t n)
{
	VPShellBakeTap bt = {};
	const bool ok = m_hdr && SnapBlock(VPSHELL_BAKETAP(m_hdr), bt);
	_snprintf_s(out, n, _TRUNCATE,
	            "bake=%s slots=%zu cache_open=%lld cache_missing=%lld cache_refused=%lld manifest_err=%lld manifests=%lld plots_changed=%lld",
	            ok && bt.state < 4 ? kBakeStateNames[bt.state] : "-", m_slots.size(), cacheOpened, cacheMissing, cacheRefused,
	            manifestErrors, manifestsApplied, plotsChanged);
}

bool ExeCacheTap::openCacheTexture(const wchar_t* name, ID3D11Texture2D*& tex, ID3D11ShaderResourceView*& srv)
{
	SafeRelease(srv);
	SafeRelease(tex);
	if(!m_dev1)
	{
		m_lastErr = "no ID3D11Device1";
		m_lastHr = E_NOINTERFACE;
		return false;
	}
	HRESULT hr = m_dev1->OpenSharedResourceByName(name, DXGI_SHARED_RESOURCE_READ, __uuidof(ID3D11Texture2D), (void**)&tex);
	if(FAILED(hr))
	{
		tex = nullptr;
		m_lastErr = "OpenSharedResourceByName";
		m_lastHr = hr;
		return false;
	}
	// Issue 3206 [L 2026-11-11]: the bake tap's copies are created in the EXE cache's own format, BC1_TYPELESS (70); a view
	// with a null description fails on a typeless texture. The view takes the typed format.
	D3D11_TEXTURE2D_DESC td;
	tex->GetDesc(&td);
	const DXGI_FORMAT vf = TypedViewFormat(td.Format);
	if(vf == DXGI_FORMAT_UNKNOWN)
	{
		SafeRelease(tex);
		m_lastErr = "typeless format without a typed view";
		m_lastHr = (HRESULT)td.Format;
		return false;
	}
	D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
	vd.Format = vf;
	vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	vd.Texture2D.MostDetailedMip = 0;
	vd.Texture2D.MipLevels = (UINT)-1;
	hr = m_dev->CreateShaderResourceView(tex, &vd, &srv);
	if(FAILED(hr))
	{
		SafeRelease(tex);
		m_lastErr = "CreateShaderResourceView";
		m_lastHr = hr;
		return false;
	}
	return true;
}

// The bake-tap block: when ACTIVE and its manifestSeq moved, (re)read the manifest; slots whose generation changed are
// re-opened by name and handed to the renderer (shell_server.cpp BakePoll).
bool ExeCacheTap::poll(c5t::Renderer& r)
{
	if(!m_hdr)
		return false;
	VPShellBakeTap bt;
	if(!SnapBlock(VPSHELL_BAKETAP(m_hdr), bt))
		return false;
	if(bt.state != m_loggedState)
	{
		RLog("terrain: bake-tap block state %s (load seq %u, %u slots, tiles %ux%u of %ux%u plots, manifest '%.64ls')",
		     bt.state < 4 ? kBakeStateNames[bt.state] : "?", bt.loadSeq, bt.slotCount, bt.tilesX, bt.tilesY, bt.tilePlotsX,
		     bt.tilePlotsY, bt.manifestName);
		m_loggedState = bt.state;
	}
	if(bt.state != VPBAKE_ACTIVE || !m_haveField)
		return false;
	const uint32_t mseq = VPSHELL_BAKETAP(m_hdr)->manifestSeq;
	if(m_manApplied && mseq == m_appliedManifestSeq)
		return false;
	auto refuse = [&](const std::string& why) {
		manifestErrors++;
		if(!m_manRefusedLogged)
			RNote("terrain: bake manifest (seq %u) refused: %s", mseq, why.c_str());
		m_manRefusedLogged = true;
		m_manApplied = true;   // not retried before the next manifestSeq
		m_appliedManifestSeq = mseq;
		return false;
	};
	if(bt.tilePlotsX != bt.tilePlotsY || bt.tilePlotsX < 1 || bt.tilePlotsX > c5t::kMaxTilePlots)
		return refuse("tile plots " + std::to_string(bt.tilePlotsX) + "x" + std::to_string(bt.tilePlotsY) + " outside 1..3 or not square");
	if(bt.tilePlotsX != m_fieldTilePlots)
	{
		if(m_fixedTilePlots)
			return refuse("tile plots " + std::to_string(bt.tilePlotsX) + " against render_tile_plots " + std::to_string(m_fixedTilePlots));
		if(m_wantTilePlots != bt.tilePlotsX)
			RLog("terrain: the bake tap's tiles are %u plots, the field's %u: asking the scene to rebuild the field's tiles", bt.tilePlotsX,
			     m_fieldTilePlots);
		m_wantTilePlots = bt.tilePlotsX;   // the scene rebuilds (wantedTilePlots) and calls onField; the manifest follows then
		return false;
	}
	m_wantTilePlots = 0;
	bt.manifestName[63] = 0;
	if(!m_manView || wcscmp(m_manName, bt.manifestName) || m_manExePid != bt.exePid)
	{
		unmapManifest();
		releaseSlots();
		wcsncpy_s(m_manName, bt.manifestName, _TRUNCATE);
		m_manExePid = bt.exePid;
		if(!MapNamedRead(m_manName, m_manSec, m_manView, m_manSize))
			return refuse("cannot open the manifest section (error " + std::to_string(GetLastError()) + ")");
	}
	VPShellBakeManifest mh;
	if(m_manSize < sizeof(mh))
		return refuse("manifest section of " + std::to_string(m_manSize) + " bytes");
	memcpy(&mh, m_manView, sizeof(mh));
	if(mh.magic != VPSHELL_BAKE_MAGIC)
		return refuse("manifest magic " + std::to_string(mh.magic));
	if(mh.version != 1)
		return refuse("manifest version " + std::to_string(mh.version) + " (want 1)");
	if(mh.slotBytes != sizeof(VPShellBakeSlot))
		return refuse("slot size " + std::to_string(mh.slotBytes));
	if(mh.slotCount > VPSHELL_BAKE_MAX_SLOTS || mh.slotCount != bt.slotCount ||
	   sizeof(mh) + (uint64_t)mh.slotCount * sizeof(VPShellBakeSlot) > m_manSize)
		return refuse("slot count " + std::to_string(mh.slotCount) + " (block " + std::to_string(bt.slotCount) + ", section " +
		              std::to_string(m_manSize) + " bytes)");
	m_manRefusedLogged = false;
	if(m_slots.size() != mh.slotCount)
	{
		releaseSlots();
		m_slots.resize(mh.slotCount);
	}
	const c5t::TileGrid tg = r.tileGrid();
	int opened = 0, missing = 0, cleared = 0;
	for(uint32_t i = 0; i < mh.slotCount; ++i)
	{
		const VPShellBakeSlot* src = (const VPShellBakeSlot*)(m_manView + sizeof(mh) + (size_t)i * sizeof(VPShellBakeSlot));
		VPShellBakeSlot sl;
		if(!SnapBlock(src, sl))
			continue;   // never written or being written: next manifestSeq
		Slot& me = m_slots[i];
		if(sl.state == 0)
		{
			if(me.opened)
			{
				r.setTileCache(me.tileX, me.tileY, c5t::TileCache());
				cleared++;
			}
			for(int k = 0; k < 2; ++k)
			{
				SafeRelease(me.srv[k]);
				SafeRelease(me.tex[k]);
			}
			me = Slot();
			continue;
		}
		if(sl.tileX >= tg.tilesX || sl.tileY >= tg.tilesY)
		{
			cacheRefused++;
			RLog("terrain: bake slot %u: tile (%u, %u) outside the field's %ux%u tiles", i, sl.tileX, sl.tileY, tg.tilesX, tg.tilesY);
			continue;
		}
		const bool changed = !me.opened || sl.generation != me.generation;
		if(changed)
		{
			if(me.opened && (me.tileX != sl.tileX || me.tileY != sl.tileY))
				r.setTileCache(me.tileX, me.tileY, c5t::TileCache());
			me.opened = false;
			bool ok = true;
			const int want = sl.state == 1 ? 2 : 1;
			for(int k = 0; k < 2; ++k)
			{
				if(k >= want)
				{
					SafeRelease(me.srv[k]);
					SafeRelease(me.tex[k]);
					continue;
				}
				wchar_t name[96];
				_snwprintf_s(name, _countof(name), _TRUNCATE, VPSHELL_BAKE_TEXTURE_FMT, bt.exePid, i, (unsigned)k);
				if(!openCacheTexture(name, me.tex[k], me.srv[k]))
				{
					ok = false;   // counted once per slot
					cacheMissing++;
					missing++;
					if(cacheMissing <= 8)
						RLog("terrain: bake slot %u: cache texture '%ls' missing (%s failed, 0x%08X): tile (%u, %u) uses the fallback", i,
						     name, m_lastErr, (unsigned)m_lastHr, sl.tileX, sl.tileY);
					break;
				}
			}
			if(!ok)
			{
				for(int k = 0; k < 2; ++k)
				{
					SafeRelease(me.srv[k]);
					SafeRelease(me.tex[k]);
				}
				me.applied = false;
				continue;
			}
			me.opened = true;
			me.generation = sl.generation;
			me.tileX = sl.tileX;
			me.tileY = sl.tileY;
			me.applied = false;
			cacheOpened++;
			opened++;
		}
		if(me.opened && !me.applied)
		{
			c5t::TileCache tc;
			tc.color = me.srv[0];
			tc.second = me.srv[1];
			tc.generation = me.generation;
			const c5r::Status st = r.setTileCache(me.tileX, me.tileY, tc);
			if(!st.ok())
			{
				cacheRefused++;
				RLog("terrain: setTileCache(%u, %u) failed: %s", me.tileX, me.tileY, st.str().c_str());
			}
			else
				me.applied = true;
		}
	}
	m_manApplied = true;
	m_appliedManifestSeq = mseq;
	manifestsApplied++;
	VPShellBakeTap* w = VPSHELL_BAKETAP(m_hdr);
	w->ackServerPid = GetCurrentProcessId();
	MemoryBarrier();
	InterlockedExchange((volatile LONG*)&w->ackManifestSeq, (LONG)mseq);
	RLog("terrain: bake manifest seq %u applied: %u slots, %d opened, %d missing, %d cleared", mseq, mh.slotCount, opened, missing, cleared);
	return true;
}

}  // namespace srvrender
