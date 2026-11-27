// proxy_session_keys.h - the one list of "our keys": the [DisableSystems] keys the patch switches off in config.ini in
// the A'' form (decisions D119, D125; issue 3523). Hand-written. SPDX-License-Identifier: GPL-3.0-or-later
//
// = shell/tools/darkred.py D33_OFF minus River and Arrows (those two stay on in config.ini; the proxy clears only their
// view bits), in darkred.py's order. Waves is not in it since 2026-11-14 (epic 3600, main 1f36a838: darkred.py keeps
// Waves in D33_ON and proxy_shell.cpp's ClearBits clears its view bit 0x200000; it was the 7th key until then). Minimap is
// not in it since 2026-11-14 (issue 0192: the EXE's own minimap panel draws, equal to stock; it was the 12th of 13 keys
// until then, rule bits 0x008BF890, A'' config mask 0xFF640767). MovementPath is not in it since 2026-11-14 (issue 3803,
// epic 0179, MC #1009: the EXE draws the move-order path's models in increment 2; it was the 8th of 12 keys until then,
// bits 0x0093F898, rule bits 0x0083F890, A'' config mask 0xFF6C0767). Particle, Combat and Projectile are not in it
// since 2026-11-15 (issue 0368, the owner's decision D147: the beta draws the EXE's own combat; they were the 8th to 10th of 11
// keys until then, bits 0x0013F898, rule bits 0x0003F890, A'' config mask 0xFFEC0767). proxy/tools/test_proxy_session.py
// compares this list with darkred.py, so the two cannot drift. Use it as an initializer:   static const char* const kKeys[] = { VP64_D33_KEYS_A2 };
// Users: proxy_session.cpp (the session rule: a D33 session = any of these keys' config-mask bits clear, the stock-off
// pair excepted) and issue 3520's config.ini routine (proxy_cfgini.cpp), which should take its key list from here.
#pragma once

#define VP64_D33_KEYS_A2                                                                                                 \
	"OverlayDebug", "Decal", "Forest", "FOW", "FluidFOW", "DistanceFog", "YieldIcons", "ColorKeyEdit"

// Of those, the keys the stock game already ships off (darkred.py STOCK_OFF; the stock config mask is 0xFFEFFFF7): a
// session with only these off is a stock session.
#define VP64_STOCK_OFF_KEYS "OverlayDebug", "ColorKeyEdit"
