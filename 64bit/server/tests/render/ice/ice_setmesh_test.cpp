// ice_setmesh_test.cpp - the ice tap reader's c5r::ice::Mesh accepted by c5r::ice::Renderer::setMesh (epic 0169, issue
// 4307; the module of 4306 checks its own input, polar-ice.md 8). Written 2026-11-15 for this repository
// (GPL-3.0-or-later). Linked with c5render (build_ice_tests.py --setmesh).
//
//   ice_setmesh_test.exe --exe EXE --game INSTALL IMAGE...
// For each image (the tap's file form): IceTapReader::loadFile, then Renderer::setMesh on a renderer initialised on a
// hardware D3D11 device with the EXE's IceShader and the install's ice textures; prints one line per image ("SETMESH ok
// <file>: <n> chunks" or "SETMESH FAIL <file>: <why>") and exits 0 when every image was accepted by both, 1 otherwise, 2 on
// bad arguments or a set-up failure. A negative is built in: the first image's mesh with one chunk's start index moved
// past the IB is offered to setMesh too and must be refused (its line "SETMESH refused as expected").
#include "srv_ice_tap.h"
#include "c5r/ice/ice.h"
#include "c5r/shaders/shadersection.h"
#include "c5r/gamefs/gamefiles.h"

#include <d3d11.h>
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	std::wstring exe, game;
	std::vector<std::string> images;
	for(int i = 1; i < argc; ++i)
	{
		std::string a = argv[i];
		if(a == "--exe" && i + 1 < argc) { std::string v = argv[++i]; exe.assign(v.begin(), v.end()); }
		else if(a == "--game" && i + 1 < argc) { std::string v = argv[++i]; game.assign(v.begin(), v.end()); }
		else images.push_back(a);
	}
	if(exe.empty() || game.empty() || images.empty()) { printf("usage: --exe EXE --game INSTALL IMAGE...\n"); return 2; }
	ID3D11Device* dev = nullptr;
	ID3D11DeviceContext* ctx = nullptr;
	D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
	if(FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
	{
		printf("SETMESH set-up: no hardware D3D11 device\n");
		return 2;
	}
	c5r::shaders::ShaderSection sec;
	c5r::gamefs::GameFiles fs;
	c5r::Status a = sec.open(exe), b = fs.open(game);
	c5r::ice::Renderer r;
	c5r::Status c = a.ok() && b.ok() ? r.init(dev, sec, fs) : c5r::Status::fail(c5r::Err::NotFound, "inputs");
	if(!a.ok() || !b.ok() || !c.ok())
	{
		printf("SETMESH set-up failed: exe %s, install %s, init %s\n", a.msg.c_str(), b.msg.c_str(), c.msg.c_str());
		ctx->Release(); dev->Release();
		return 2;
	}
	int bad = 0;
	bool negDone = false;
	for(const std::string& f : images)
	{
		c5r::ice::Mesh m;
		std::string why;
		c5r::Status s = srvrender::IceTapReader::loadFile(std::wstring(f.begin(), f.end()), m, why);
		if(!s.ok()) { printf("SETMESH FAIL %s: the reader refused it: %s\n", f.c_str(), why.c_str()); bad++; continue; }
		s = r.setMesh(m);
		if(!s.ok() || !r.hasMesh() || r.chunkCount() != m.chunks.size())
		{
			printf("SETMESH FAIL %s: setMesh: %s\n", f.c_str(), s.msg.c_str());
			bad++;
			continue;
		}
		uint64_t tris = 0;
		for(const c5r::ice::Chunk& k : m.chunks) tris += k.triangles;
		printf("SETMESH ok %s: %u chunks, %llu triangles, VB %zu bytes, IB %zu bytes\n", f.c_str(), r.chunkCount(),
		       (unsigned long long)tris, m.vb.size(), m.ib.size());
		if(!negDone)
		{
			negDone = true;
			c5r::ice::Mesh n = m;
			n.chunks.back().startIndex = (uint32_t)(n.ib.size() / 2);
			c5r::Status ns = r.setMesh(n);
			if(ns.ok() || r.chunkCount() != m.chunks.size()) { printf("SETMESH FAIL negative: a chunk past the IB was accepted\n"); bad++; }
			else printf("SETMESH refused as expected: %s\n", ns.msg.c_str());
		}
	}
	r.release();
	ctx->Release();
	dev->Release();
	printf("SETMESH %d of %zu images failed\n", bad, images.size());
	return bad ? 1 : 0;
}
