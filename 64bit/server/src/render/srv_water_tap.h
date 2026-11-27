// srv_water_tap.h - server.exe's reader of the water mesh tap (epic 3600, issue 3603; decision 1 route a).
//
// Interface committed by the team-3600 lead; filled in by issue 3603 (owner of srv_water_tap.h / .cpp). Producer:
// shell/composite/water_tap.cpp. Layout and ack protocol: VPShellWaterTap / VPShellWaterImage / VPShellWaterRecord in
// shell/common/shell_shared.h. The reader checks everything before it trusts a byte, and acks every copy it looked at,
// refused ones too (ackResult): block state READY and loadSeq = the camera block's gridSeq (else stale, -3); the block's
// counts within the limits (BadLayout); the section opens (-2) and its view holds imageBytes (Truncated); magic and
// version, header and record sizes (-4); image and block agree (bytes, records, load, pid: BadLayout); every data block
// inside the image after the records (Truncated); VB stride x count and IB format x count = the block's bytes, every
// draw's buffers exist with their roles and every index of a draw inside its VB (BadLayout); every record's hash and the
// image hash (-1; a block without an image hash is refused too); the map size against the camera block (BadLayout).
// Consumer: the water scene (issue 3607), which turns the records into c5r::water::Mesh and keeps the placeholder
// plane when the tap is absent or refused (logged once per load). What the records hold: water-system.md section 9.
#pragma once
#include "srv_render_common.h"
#include <stdarg.h>
#include <cstdint>
#include <string>
#include <vector>

namespace srvrender {

// ackResult values beside c5r::Err (> 0): the height-field tap's codes, plus -4 for magic / version / sizes.
enum { WTACK_OK = 0, WTACK_HASH = -1, WTACK_OPEN = -2, WTACK_STALE = -3, WTACK_VERSION = -4 };

struct WaterTapRecordCopy
{
	VPShellWaterRecord rec;        // as published (dataOffset then refers to the image, not to `data`)
	std::vector<uint8_t> data;     // the record's data block, copied out of the section
};

// Epic 0301, issue 3903 (research/06-tooling-and-workflows/offline-scene-harness.md 4.1): the bytes the live poll()
// accepted, handed out after every check passed (CheckImage and the map size) and before the section is unmapped: the
// block as SnapBlock returned it, the image (block.imageBytes bytes, VPShellWaterImage header first) and the camera
// block's grid values the checks used (haveCam false: gridSeq / samplesX / samplesY are 0). The sink must not keep the
// pointer. Null (the default): no recorder; the live path then costs one pointer test per accepted copy.
using WaterImageSink = void (*)(void* ctx, const VPShellWaterTap& block, const uint8_t* image, size_t imageBytes, bool haveCam,
                                uint32_t gridSeq, uint32_t samplesX, uint32_t samplesY);

class WaterTapReader
{
public:
	explicit WaterTapReader(VPShellHeader* hdr) : m_hdr(hdr) {}
	// Every loop iteration: true when a new image of a new load (or a re-requested copy) was read and acked. Never
	// throws; a refused image is acked with its ackResult and counted in `refused`.
	bool poll();
	// issue 3903: the recorder's hand-off point (above).
	void setImageSink(WaterImageSink f, void* ctx)
	{
		m_sink = f;
		m_sinkCtx = ctx;
	}
	// issue 3903, the file source: the checks poll() runs after its handshake (the camera's grid seq against the block's
	// load seq, the block's limits, CheckImage on image[0, imageBytes), the map size against samplesX / samplesY) on an
	// image recorded by the sink. No section, no ack, no request. True: records() and image() replaced, loadSeq() =
	// block.loadSeq, accepted++; false: refused++, lastResult() / lastWhy() as poll() sets them. Never throws.
	bool loadImage(const VPShellWaterTap& block, const uint8_t* image, size_t imageBytes, bool haveCam, uint32_t gridSeq,
	               uint32_t samplesX, uint32_t samplesY);
	// The records of the last accepted image (empty before one).
	const std::vector<WaterTapRecordCopy>& records() const { return m_records; }
	const VPShellWaterImage& image() const { return m_image; }
	uint32_t loadSeq() const { return m_loadSeq; }
	int32_t lastResult() const { return m_lastResult; }
	const std::string& lastWhy() const { return m_lastWhy; }
	int64_t accepted = 0, refused = 0, requests = 0;

private:
	void writeAck(uint32_t loadSeq, uint32_t copySeq, int32_t result);
	VPShellHeader* m_hdr = nullptr;
	std::vector<WaterTapRecordCopy> m_records;
	VPShellWaterImage m_image = {};
	uint32_t m_loadSeq = 0;
	int32_t m_lastResult = 0;
	std::string m_lastWhy;
	uint32_t m_loggedState = 0xFFFFFFFFu;
	bool m_haveLoad = false, m_ackedAny = false, m_requested = false;
	uint32_t m_ackedLoad = 0, m_ackedCopy = 0, m_reqLoad = 0, m_reqCopy = 0;
	WaterImageSink m_sink = nullptr;   // issue 3903
	void* m_sinkCtx = nullptr;
};

} // namespace srvrender
