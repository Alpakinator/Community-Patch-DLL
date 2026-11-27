// srv_model_tap.cpp - server.exe's side of the model tap: the frame reader and the identity-set publisher (epic 0561,
// ticket 4875). Contract: srv_model_tap.h. Written 2026-11-18 for this repository (GPL-3.0-or-later), by the pattern of
// srv_decal_tap.cpp. Layout: shell/common/shell_model_tap.h; research/05-graphics-composition/city-renderer.md 11.
#include "srv_model_tap.h"

#include <algorithm>
#include <new>
#include <stdarg.h>

namespace srvmodels {

namespace {

using srvrender::Qpc;
using srvrender::QpcFreq;
using srvrender::QpcToMs;
using srvrender::RNote;

const char* kStateNames[] = {"NONE", "RUNNING", "FAILED", "OFF"};
const char* kRefusalNames[MREF_COUNT_] = {"none", "block_magic", "block_version", "block", "open", "slot", "torn", "seq",
                                          "hash", "content", "memory"};

std::string Fmt(const char* fmt, ...)
{
	char b[512];
	va_list a;
	va_start(a, fmt);
	_vsnprintf_s(b, sizeof(b), _TRUNCATE, fmt, a);
	va_end(a);
	return b;
}

enum SnapResult { SNAP_NEVER, SNAP_BUSY, SNAP_OK };

SnapResult SnapControl(const VPShellModelTapControl* src, VPShellModelTapControl& out)
{
	const volatile uint32_t* seq = (const volatile uint32_t*)&src->seq;
	for(int i = 0; i < 16; ++i)
	{
		const uint32_t s1 = *seq;
		if(s1 == 0)
			return SNAP_NEVER;
		if(s1 & 1)
		{
			YieldProcessor();
			continue;
		}
		MemoryBarrier();
		memcpy(&out, (const void*)src, sizeof(out));
		MemoryBarrier();
		if(*seq == s1)
		{
			out.seq = s1;
			return SNAP_OK;
		}
	}
	return SNAP_BUSY;
}

bool KeyLess(const VPModelSetKey& a, const VPModelSetKey& b) { return memcmp(&a.key, &b.key, sizeof(VPModelKey)) < 0; }
bool KeyEq(const VPModelSetKey& a, const VPModelSetKey& b) { return memcmp(&a.key, &b.key, sizeof(VPModelKey)) == 0; }

}  // namespace

const char* ModelTapRefusalName(uint32_t r) { return r < MREF_COUNT_ ? kRefusalNames[r] : "?"; }

int32_t ModelTapAckResult(uint32_t r, c5r::Err content)
{
	switch(r)
	{
	case MREF_NONE: return MTACK_OK;
	case MREF_BLOCK_MAGIC: case MREF_BLOCK_VERSION: return MTACK_VERSION;
	case MREF_OPEN: return MTACK_OPEN;
	case MREF_TORN: case MREF_SEQ: return MTACK_TORN;
	case MREF_HASH: return MTACK_HASH;
	case MREF_MEMORY: return (int32_t)c5r::Err::OutOfMemory;
	case MREF_CONTENT: return content == c5r::Err::Ok ? (int32_t)c5r::Err::BadLayout : (int32_t)content;
	default: return (int32_t)c5r::Err::BadLayout;
	}
}

// ---- the reader ----------------------------------------------------------------------------------------------------

ModelTapReader::ModelTapReader(uint32_t exePid) : m_pid(exePid)
{
	wchar_t name[64];
	_snwprintf_s(name, _countof(name), _TRUNCATE, VPSHELL_MODELTAP_CONTROL_FMT, exePid);
	m_controlName = name;
	_snwprintf_s(name, _countof(name), _TRUNCATE, VPSHELL_MODELTAP_DATA_FMT, exePid);
	m_dataName = name;
}

ModelTapReader::ModelTapReader(uint32_t exePid, const std::wstring& controlName, const std::wstring& dataName)
    : m_pid(exePid), m_controlName(controlName), m_dataName(dataName)
{
}

ModelTapReader::~ModelTapReader()
{
	if(m_view)
		UnmapViewOfFile(m_view);
	if(m_data)
		CloseHandle(m_data);
	if(m_block)
		UnmapViewOfFile(m_block);
	if(m_control)
		CloseHandle(m_control);
}

bool ModelTapReader::openControl()
{
	const int64_t now = Qpc();
	if(now < m_nextOpenQpc)
		return false;
	m_nextOpenQpc = now + QpcFreq() / 4;   // a missing producer is looked for again in 250 ms
	HANDLE h = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, m_controlName.c_str());
	if(!h)
		return false;
	void* v = MapViewOfFile(h, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, VPSHELL_MODELTAP_CONTROL_BYTES);
	if(!v)
	{
		RNote("models: control section '%ls' opens but does not map %u bytes (error %lu)", m_controlName.c_str(),
		      VPSHELL_MODELTAP_CONTROL_BYTES, GetLastError());
		CloseHandle(h);
		return false;
	}
	m_control = h;
	m_block = (VPShellModelTapControl*)v;
	m_c.controlOpens++;
	InterlockedExchange((volatile LONG*)&m_block->wantBits, (LONG)m_want);
	RNote("models: control section '%ls' opened (want bits 0x%X)", m_controlName.c_str(), m_want);
	return true;
}

bool ModelTapReader::openData(const VPShellModelTapControl& tb, std::string& why)
{
	const size_t need = (size_t)tb.slots * tb.slotBytes;
	if(m_view && m_viewBytes >= need)
		return true;
	if(m_view)
	{
		UnmapViewOfFile(m_view);
		m_view = nullptr;
		m_viewBytes = 0;
	}
	if(m_data)
	{
		CloseHandle(m_data);
		m_data = nullptr;
	}
	HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, m_dataName.c_str());
	if(!h)
	{
		why = Fmt("cannot open the frame section '%ls' (error %lu)", m_dataName.c_str(), GetLastError());
		return false;
	}
	const uint8_t* v = (const uint8_t*)MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0);
	if(!v)
	{
		why = Fmt("cannot map the frame section '%ls' (error %lu)", m_dataName.c_str(), GetLastError());
		CloseHandle(h);
		return false;
	}
	MEMORY_BASIC_INFORMATION mbi = {};
	const size_t size = VirtualQuery(v, &mbi, sizeof(mbi)) ? mbi.RegionSize : 0;
	if(size < need)
	{
		why = Fmt("the frame section '%ls' holds %llu bytes, the control block names %u slots of %u bytes", m_dataName.c_str(),
		          (unsigned long long)size, tb.slots, tb.slotBytes);
		UnmapViewOfFile(v);
		CloseHandle(h);
		return false;
	}
	m_data = h;
	m_view = v;
	m_viewBytes = size;
	m_c.dataOpens++;
	RNote("models: frame section '%ls' opened (%u slots of %u bytes)", m_dataName.c_str(), tb.slots, tb.slotBytes);
	return true;
}

void ModelTapReader::setWantBits(uint32_t bits)
{
	bits &= VPMODE_CITIES | VPMODE_LANDMARKS;
	m_want = bits;
	if(m_block && m_block->wantBits != bits)
		InterlockedExchange((volatile LONG*)&m_block->wantBits, (LONG)bits);
}

void ModelTapReader::writeAck(uint64_t frameSeq, int32_t result)
{
	VPShellModelTapControl* t = m_block;
	t->ackServerPid = GetCurrentProcessId();
	InterlockedExchange((volatile LONG*)&t->ackResult, result);
	MemoryBarrier();
	InterlockedExchange64((volatile LONG64*)&t->ackFrameSeq, (LONG64)frameSeq);   // written last
}

void ModelTapReader::refuse(uint32_t reason, c5r::Err content, const std::string& why, bool fromFile)
{
	if(fromFile)
		m_c.fileRefused++;
	else
		m_c.refused++;
	if(reason < MREF_COUNT_)
		m_c.refusedBy[reason]++;
	if(reason == MREF_CONTENT && (uint32_t)content < 16)
		m_c.contentBy[(uint32_t)content]++;
	m_lastResult = ModelTapAckResult(reason, content);
	m_lastWhy = why;
	// LOUD for the first 20 refusals, then every 100th (a steady refusal would flood the log at the frame rate)
	const uint64_t n = m_c.refused + m_c.fileRefused;
	if(n <= 20 || n % 100 == 0)
		RNote("LOUD: models: %s refused (%s, ackResult %d; %llu refused so far): %s", fromFile ? "file image" : "tap frame",
		      ModelTapRefusalName(reason), m_lastResult, (unsigned long long)n, why.c_str());
}

void ModelTapReader::accept(std::vector<uint8_t>&& raw, uint64_t frameSeq)
{
	std::shared_ptr<std::vector<uint8_t>> img;
	try
	{
		img = std::make_shared<std::vector<uint8_t>>(std::move(raw));
	}
	catch(...)
	{
		return;   // the old image stays (the Validator accepted it already: only memory can fail here)
	}
	m_image = img;
	m_frameSeq = frameSeq;
	m_acceptedQpc = Qpc();
	m_lastResult = 0;
	m_lastWhy.clear();
}

bool ModelTapReader::poll()
{
	m_c.polls++;
	if(!m_block && !openControl())
		return false;
	VPShellModelTapControl tb;
	const SnapResult sr = SnapControl(m_block, tb);
	if(sr == SNAP_NEVER)
	{
		m_c.never++;
		return false;
	}
	if(sr == SNAP_BUSY)
	{
		m_c.busy++;
		return false;
	}
	m_last = tb;
	m_haveLast = true;
	if(tb.state != m_loggedState)
	{
		RNote("models: tap state %s (fail reason %u, frame %llu, %u slots of %u bytes)", tb.state < 4 ? kStateNames[tb.state] : "?",
		      tb.failReason, (unsigned long long)tb.frameSeq, tb.slots, tb.slotBytes);
		m_loggedState = tb.state;
	}
	if(m_block->wantBits != m_want)   // a restarted compositor made a new block: our want bits again
		InterlockedExchange((volatile LONG*)&m_block->wantBits, (LONG)m_want);
	if(tb.state != VPMTAP_RUNNING)
	{
		m_c.notRunning++;
		return false;
	}
	if(tb.frameSeq == 0 || tb.frameSeq == m_ackedSeq)
		return false;
	const int64_t q0 = Qpc();
	if(m_ackedSeq && tb.frameSeq > m_ackedSeq + 1)
		m_c.skipped += tb.frameSeq - m_ackedSeq - 1;
	uint32_t reason = MREF_NONE;
	c5r::Err content = c5r::Err::Ok;
	std::string why;
	if(tb.magic != VPSHELL_MODELTAP_MAGIC)
	{
		reason = MREF_BLOCK_MAGIC;
		why = Fmt("control block magic 0x%08X, expected 0x%08X", tb.magic, VPSHELL_MODELTAP_MAGIC);
	}
	else if(tb.version != VPSHELL_MODELTAP_VERSION)
	{
		reason = MREF_BLOCK_VERSION;
		why = Fmt("control block version %u, this reader reads %u", tb.version, VPSHELL_MODELTAP_VERSION);
	}
	else if(tb.exePid != m_pid || tb.slots != VPSHELL_MODELTAP_SLOTS || tb.slotBytes < VPSHELL_MODELTAP_SLOT_KB_MIN * 1024u ||
	        tb.slotBytes > VPSHELL_MODELTAP_SLOT_KB_MAX * 1024u || (tb.slotBytes & 15u))
	{
		reason = MREF_BLOCK;
		why = Fmt("control block: pid %u (expected %u), %u slots (expected %u), slot bytes %u (%u..%u KB, 16-byte multiple)",
		          tb.exePid, m_pid, tb.slots, VPSHELL_MODELTAP_SLOTS, tb.slotBytes, VPSHELL_MODELTAP_SLOT_KB_MIN,
		          VPSHELL_MODELTAP_SLOT_KB_MAX);
	}
	else if(tb.slot != (uint32_t)(tb.frameSeq % tb.slots) || tb.frameBytes < sizeof(VPModelTapFrame) || tb.frameBytes > tb.slotBytes)
	{
		reason = MREF_SLOT;
		why = Fmt("frame %llu in slot %u (expected %u), %u bytes (header %u, slot %u)", (unsigned long long)tb.frameSeq, tb.slot,
		          (uint32_t)(tb.frameSeq % tb.slots), tb.frameBytes, (uint32_t)sizeof(VPModelTapFrame), tb.slotBytes);
	}
	else if(!openData(tb, why))
		reason = MREF_OPEN;
	std::vector<uint8_t> raw;
	if(!reason)
	{
		try
		{
			const uint8_t* src = m_view + (size_t)tb.slot * tb.slotBytes;
			raw.assign(src, src + tb.frameBytes);   // copy first: every check runs on bytes no writer can change
			m_c.bytesCopied += tb.frameBytes;
		}
		catch(...)
		{
			reason = MREF_MEMORY;
			why = Fmt("cannot allocate %u bytes for the frame copy", tb.frameBytes);
		}
	}
	if(!reason)
	{
		// The producer writes frame n + 3 into our slot only after it published n + 2; seen before the copy ended, the
		// copy may be torn (the header's frameSeq and the hash would most likely say so too).
		// (The slot of frame m is written while the block still names m - 1, so "the block names n + 2 or later" covers
		// a publication in progress too.)
		MemoryBarrier();
		const uint64_t now = *(const volatile uint64_t*)&m_block->frameSeq;
		if(now >= tb.frameSeq + 2)
		{
			reason = MREF_TORN;
			why = Fmt("frame %llu: the producer reached frame %llu while it was copied", (unsigned long long)tb.frameSeq,
			          (unsigned long long)now);
		}
	}
	if(!reason)
	{
		VPModelTapFrame h;
		memcpy(&h, raw.data(), sizeof(h));
		if(h.frameSeq != tb.frameSeq)
		{
			reason = MREF_SEQ;
			why = Fmt("the slot holds frame %llu, the control block names %llu", (unsigned long long)h.frameSeq,
			          (unsigned long long)tb.frameSeq);
		}
		else if(h.fnv1a64 != tb.fnv1a64)
		{
			reason = MREF_HASH;
			why = Fmt("frame %llu: image hash 0x%016llX, the control block's 0x%016llX", (unsigned long long)tb.frameSeq,
			          (unsigned long long)h.fnv1a64, (unsigned long long)tb.fnv1a64);
		}
	}
	if(!reason)
	{
		c5r::Status st = m_val ? m_val(m_valCtx, raw.data(), raw.size())
		                       : c5r::Status::fail(c5r::Err::Unsupported, "no content check (no model layer)");
		if(!st.ok())
		{
			reason = MREF_CONTENT;
			content = st.code;
			why = Fmt("frame %llu: ", (unsigned long long)tb.frameSeq) + st.str();
		}
	}
	writeAck(tb.frameSeq, ModelTapAckResult(reason, content));
	m_ackedSeq = tb.frameSeq;
	const double ms = QpcToMs(Qpc() - q0);
	m_c.lastCopyMs = ms;
	if(ms > m_c.maxCopyMs)
		m_c.maxCopyMs = ms;
	if(reason)
	{
		refuse(reason, content, why, false);
		return false;
	}
	m_c.accepted++;
	accept(std::move(raw), tb.frameSeq);
	return true;
}

c5r::Status ModelTapReader::loadImage(const uint8_t* image, size_t n)
{
	std::vector<uint8_t> raw;
	try
	{
		if(image && n)
			raw.assign(image, image + n);
	}
	catch(...)
	{
		refuse(MREF_MEMORY, c5r::Err::Ok, "cannot allocate the image copy", true);
		return c5r::Status::fail(c5r::Err::OutOfMemory, "cannot allocate the image copy");
	}
	c5r::Status st = m_val ? m_val(m_valCtx, raw.data(), raw.size())
	                       : c5r::Status::fail(c5r::Err::Unsupported, "no content check (no model layer)");
	if(!st.ok())
	{
		refuse(MREF_CONTENT, st.code, st.str(), true);
		return st;
	}
	uint64_t seq = 0;
	if(raw.size() >= sizeof(VPModelTapFrame))
		memcpy(&seq, raw.data() + offsetof(VPModelTapFrame, frameSeq), sizeof(seq));
	m_c.fileLoads++;
	accept(std::move(raw), seq);
	return c5r::Status::Ok();
}

c5r::Status ModelTapReader::writeCurrentImage(const std::wstring& path) const
{
	const std::shared_ptr<const std::vector<uint8_t>> raw = m_image;
	if(!raw)
		return c5r::Status::fail(c5r::Err::NotFound, "no model tap frame yet");
	return WriteModelImage(*raw, path);
}

c5r::Status WriteModelImage(const std::vector<uint8_t>& raw, const std::wstring& path)
{
	const std::wstring tmp = path + L".tmp";
	HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(f == INVALID_HANDLE_VALUE)
		return c5r::Status::fail(c5r::Err::Io, Fmt("cannot create '%ls' (error %lu)", tmp.c_str(), GetLastError()));
	size_t done = 0;
	bool ok = true;
	while(done < raw.size())
	{
		DWORD w = 0;
		const DWORD want = (DWORD)std::min<size_t>(raw.size() - done, 1u << 24);
		if(!WriteFile(f, raw.data() + done, want, &w, nullptr) || w == 0)
		{
			ok = false;
			break;
		}
		done += w;
	}
	ok = CloseHandle(f) && ok;
	if(!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		const DWORD le = GetLastError();
		DeleteFileW(tmp.c_str());
		return c5r::Status::fail(c5r::Err::Io, Fmt("cannot write '%ls' (error %lu)", path.c_str(), le));
	}
	return c5r::Status::Ok();
}

// ---- the identity set ----------------------------------------------------------------------------------------------

std::vector<VPModelSetKey> BuildIdentitySet(const std::vector<KeyInfo>& in, uint32_t bits, SetBuildReport* rep)
{
	SetBuildReport r;
	r.in = (uint32_t)in.size();
	std::vector<VPModelSetKey> all;
	all.reserve(in.size());
	for(const KeyInfo& k : in)
	{
		if(k.source == KeySource::Wall || k.source == KeySource::GreatWall)
		{
			r.cpuBuilt++;
			continue;
		}
		if(!k.key.ib || !k.key.vb)
		{
			r.noKey++;
			continue;
		}
		VPModelSetKey e;
		memset(&e, 0, sizeof(e));
		e.key = k.key;
		if(k.source == KeySource::CpuCopied && (k.skinned || k.family == KeyFamily::Landmark))
		{
			r.noKey++;   // a CPU-built mesh is a block or material-3 draw (city art): anything else is not one
			continue;
		}
		if(k.skinned)
		{
			e.cls = (uint8_t)VPMODEL_CLASS_SKINNED;
			e.bits = (uint8_t)(k.source == KeySource::LandmarkRow ? VPMODE_LANDMARKS : VPMODE_CITIES);
		}
		else if(k.family == KeyFamily::CityBlock)
		{
			e.cls = (uint8_t)VPMODEL_CLASS_BLOCK;
			e.bits = (uint8_t)VPMODE_CITIES;
		}
		else if(k.family == KeyFamily::CityPiece)
		{
			e.cls = (uint8_t)VPMODEL_CLASS_SINGLE;
			e.bits = (uint8_t)VPMODE_CITIES;
		}
		else
		{
			e.cls = (uint8_t)VPMODEL_CLASS_INST;
			e.bits = (uint8_t)VPMODE_LANDMARKS;
		}
		if(!(e.bits & bits))
		{
			r.classOff++;
			continue;
		}
		if(k.source == KeySource::CpuCopied)
			r.cpuCopied++;
		all.push_back(e);
	}
	std::stable_sort(all.begin(), all.end(), KeyLess);
	std::vector<VPModelSetKey> out;
	out.reserve(all.size());
	for(size_t i = 0; i < all.size();)
	{
		size_t j = i + 1;
		bool conflict = false;
		while(j < all.size() && KeyEq(all[i], all[j]))
		{
			if(all[j].bits != all[i].bits || all[j].cls != all[i].cls)
				conflict = true;
			++j;
		}
		if(conflict)
			r.conflicts += (uint32_t)(j - i);
		else
		{
			r.duplicates += (uint32_t)(j - i - 1);
			if(out.size() >= VPSHELL_MODELSET_MAX_KEYS)
				r.overLimit++;
			else
			{
				out.push_back(all[i]);
				if(all[i].bits & VPMODE_CITIES)
					r.cities++;
				else
					r.landmarks++;
			}
		}
		i = j;
	}
	r.out = (uint32_t)out.size();
	if(rep)
		*rep = r;
	return out;
}

ModelSetPublisher::ModelSetPublisher(uint32_t exePid) : m_pid(exePid)
{
	wchar_t name[64];
	_snwprintf_s(name, _countof(name), _TRUNCATE, VPSHELL_MODELSET_FMT, exePid);
	m_name = name;
}

ModelSetPublisher::ModelSetPublisher(uint32_t exePid, const std::wstring& name) : m_name(name), m_pid(exePid) {}

ModelSetPublisher::~ModelSetPublisher()
{
	if(m_view)
		UnmapViewOfFile(m_view);
	if(m_sec)
		CloseHandle(m_sec);
}

bool ModelSetPublisher::create()
{
	if(m_view)
		return true;
	m_c.creates++;
	HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, VPSHELL_MODELSET_MAX_BYTES, m_name.c_str());
	if(!h)
	{
		m_c.createFailed++;
		RNote("LOUD: models: cannot create the identity set section '%ls' (error %lu)", m_name.c_str(), GetLastError());
		return false;
	}
	const bool existed = GetLastError() == ERROR_ALREADY_EXISTS;
	uint8_t* v = (uint8_t*)MapViewOfFile(h, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, VPSHELL_MODELSET_MAX_BYTES);
	if(!v)
	{
		m_c.createFailed++;
		RNote("LOUD: models: the identity set section '%ls' does not map (error %lu)", m_name.c_str(), GetLastError());
		CloseHandle(h);
		return false;
	}
	m_sec = h;
	m_view = v;
	if(existed)
	{
		// A section of an earlier server for the same EXE (kept open by the compositor's copy in progress, or a server
		// restarted under the same EXE): continue its generations, so the compositor never sees one again.
		const VPModelSetHeader* hd = (const VPModelSetHeader*)v;
		if(hd->magic == VPSHELL_MODELSET_MAGIC && hd->generation > m_gen)
			m_gen = hd->generation;
	}
	RNote("models: identity set section '%ls' %s (%u bytes) for the EXE's pid %u", m_name.c_str(),
	      existed ? "opened (it existed)" : "created", VPSHELL_MODELSET_MAX_BYTES, m_pid);
	return true;
}

c5r::Status ModelSetPublisher::publish(const std::vector<VPModelSetKey>& keys, VPShellModelTapControl* control)
{
	if(keys.size() > VPSHELL_MODELSET_MAX_KEYS)
		return c5r::Status::fail(c5r::Err::BadLayout, Fmt("%llu keys, at most %u", (unsigned long long)keys.size(),
		                                                  VPSHELL_MODELSET_MAX_KEYS));
	if(!create())
		return c5r::Status::fail(c5r::Err::Io, "the identity set section cannot be made");
	VPModelSetHeader* hd = (VPModelSetHeader*)m_view;
	const uint32_t gen = m_gen + 1;
	const uint32_t s0 = hd->seq;
	InterlockedExchange((volatile LONG*)&hd->seq, (LONG)((s0 | 1u) + ((s0 & 1u) ? 2u : 0u)));   // odd: writing
	MemoryBarrier();
	const size_t bytes = keys.size() * sizeof(VPModelSetKey);
	if(bytes)
		memcpy(m_view + 64, keys.data(), bytes);
	hd->magic = VPSHELL_MODELSET_MAGIC;
	hd->version = VPSHELL_MODELSET_VERSION;
	hd->headerBytes = sizeof(VPModelSetHeader);
	hd->keyBytes = sizeof(VPModelSetKey);
	hd->generation = gen;
	hd->keyCount = (uint32_t)keys.size();
	hd->keyOffset = 64;
	hd->serverPid = GetCurrentProcessId();
	hd->pad0 = 0;
	hd->fnv1a64 = VPModelHash64(m_view + 64, bytes);
	memset(hd->reserved, 0, sizeof(hd->reserved));
	MemoryBarrier();
	InterlockedExchange((volatile LONG*)&hd->seq, (LONG)(hd->seq + 1));   // even: complete
	m_gen = gen;
	m_c.published++;
	m_c.generation = gen;
	m_c.keys = (uint32_t)keys.size();
	if(control)
	{
		m_readsAtPublish = ((const volatile VPShellModelTapControl*)control)->setReads;
		MemoryBarrier();
		InterlockedExchange((volatile LONG*)&control->setGeneration, (LONG)gen);   // last
	}
	RNote("models: identity set generation %u published (%u keys)", gen, (uint32_t)keys.size());
	return c5r::Status::Ok();
}

bool ModelSetPublisher::acked(const VPShellModelTapControl* control)
{
	if(!control || !m_gen)
		return false;
	uint32_t gen = 0, res = 0;
	for(int i = 0; i < 8; ++i)
	{
		const uint32_t s1 = ((const volatile VPShellModelTapControl*)control)->seq;
		if(s1 & 1)
		{
			YieldProcessor();
			continue;
		}
		MemoryBarrier();
		gen = ((const volatile VPShellModelTapControl*)control)->setGenerationApplied;
		res = ((const volatile VPShellModelTapControl*)control)->setResult;
		MemoryBarrier();
		if(((const volatile VPShellModelTapControl*)control)->seq == s1)
			break;
	}
	if(gen == m_gen && res == VPMSET_OK)
	{
		if(m_verdictGen != m_gen)
		{
			m_verdictGen = m_gen;
			m_c.verdicts++;
			m_c.lastResult = res;
		}
		return true;
	}
	// a refusal names our generation only through setResult with setGenerationApplied left at an older one: counted once
	// per generation, and only once the compositor has read a set since our publish (setReads moved), so an older
	// generation's refusal still standing in setResult is not taken for this one's
	const uint32_t reads = ((const volatile VPShellModelTapControl*)control)->setReads;
	if(res != VPMSET_OK && res != VPMSET_NONE && m_verdictGen != m_gen && reads != m_readsAtPublish)
	{
		m_verdictGen = m_gen;
		m_c.verdicts++;
		m_c.lastResult = res;
		if(res < VPMSET_RESULT_COUNT)
			m_c.refusedBy[res]++;
		RNote("LOUD: models: the compositor refused identity set generation %u (setResult %u)", m_gen, res);
	}
	return false;
}

uint32_t CheckModelSet(const uint8_t* section, size_t bytes, uint32_t serverPid, std::vector<VPModelSetKey>* out)
{
	if(!section || bytes < sizeof(VPModelSetHeader))
		return VPMSET_BOUNDS;
	VPModelSetHeader h;
	const volatile uint32_t* seq = (const volatile uint32_t*)section;
	const uint32_t s1 = *seq;
	if(s1 & 1)
		return VPMSET_TORN;
	MemoryBarrier();
	memcpy(&h, section, sizeof(h));
	if(h.magic != VPSHELL_MODELSET_MAGIC)
		return VPMSET_MAGIC;
	if(h.version != VPSHELL_MODELSET_VERSION || h.headerBytes != sizeof(VPModelSetHeader) || h.keyBytes != sizeof(VPModelSetKey))
		return VPMSET_VERSION;
	if(h.serverPid != serverPid)
		return VPMSET_PID;
	if(h.keyCount > VPSHELL_MODELSET_MAX_KEYS || h.keyOffset != 64 ||
	   (uint64_t)h.keyOffset + (uint64_t)h.keyCount * sizeof(VPModelSetKey) > bytes)
		return VPMSET_BOUNDS;
	std::vector<VPModelSetKey> keys(h.keyCount);
	if(h.keyCount)
		memcpy(keys.data(), section + h.keyOffset, (size_t)h.keyCount * sizeof(VPModelSetKey));
	MemoryBarrier();
	if(*seq != s1)
		return VPMSET_TORN;
	if(VPModelHash64(keys.data(), keys.size() * sizeof(VPModelSetKey)) != h.fnv1a64)
		return VPMSET_HASH;
	for(const VPModelSetKey& k : keys)
	{
		const uint32_t b = k.bits & (VPMODE_CITIES | VPMODE_LANDMARKS);
		if(!b || (k.bits & ~(VPMODE_CITIES | VPMODE_LANDMARKS)) || (k.cls == VPMODEL_CLASS_SKINNED && b == (VPMODE_CITIES | VPMODE_LANDMARKS)))
			return VPMSET_BOUNDS;
	}
	if(out)
		out->swap(keys);
	return VPMSET_OK;
}

}  // namespace srvmodels
