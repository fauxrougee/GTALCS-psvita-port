#pragma once

// reLCS Benchmark: standalone automatic benchmark, in the spirit of 3DMark.
// The engine boots straight into scripted, repeatable test scenes, measures
// every frame, then writes a report saying where the frame time goes.
//
// Built when RELCS_BENCHMARK is defined:
//   - Vita: the separate "reLCS Benchmark" VPK (TITLE_ID RLCSBENCH,
//     cmake -DVITA_BENCHMARK=ON, tools/vita/build.py --benchmark). Always active.
//   - Windows: every build defines it; the benchmark runs only when the exe is
//     started with -benchmark (optionally -benchmark=<test list>), from assets/.
// The normal Vita game build does not define it: every call site below is
// wrapped in #ifdef RELCS_BENCHMARK and compiles to nothing.
// User documentation: docs/BENCHMARK.md.

#ifdef RELCS_BENCHMARK

namespace Bench {

// ---- Activation -------------------------------------------------------------

// Vita benchmark build: always true. PC: true after "-benchmark[=list]".
bool Active(void);
// rsPreInitCommandLine (skeleton.cpp): consumes "-benchmark" and
// "-benchmark=<comma separated test or group ids>". Returns true if consumed.
bool PreInitCommandLine(const char *arg);

// ---- Boot (all no-ops when !Active()) -----------------------------------------

// Right after FrontEndMenuManager.LoadSettings() (psInitialize, glfw.cpp and
// win.cpp): force benchmark settings in memory (vsync off, frame limiter off,
// draw distance, densities, HUD...). They are never written back.
void OnSettingsLoaded(void);
// SaveINISettings / SaveINIControllerSettings / CMenuManager::SaveSettings
// return early when this is false: the user's reLCS.ini is never touched.
bool AllowSettingsSave(void);
// GS_INIT_ONCE: true = go straight to GS_INIT_PLAYING_GAME (no front end).
bool SkipFrontend(void);
// GS_INIT_PLAYING_GAME, after InitialiseGame(), just before GS_PLAYING_GAME.
void OnGameInitialised(void);

// ---- Per frame, GS_PLAYING_GAME -----------------------------------------------

// Around RsEventHandler(rsIDLE) in glfw.cpp and win.cpp. On Vita FrameEnd is
// called before VitaPerfIdleEnd (the section times of the frame are read
// before the profiler commits or drops them).
void FrameBegin(void);
void FrameEnd(void);
// Idle (main.cpp), right after CTimer::Update(): fixed time step.
void AfterTimerUpdate(void);
// CGame::Process, right after CPad::UpdatePads(): input lock and abort combo.
void AfterPadUpdate(void);
// CGame::Process: gate for CTheScripts::Process().
bool ScriptsEnabled(void);
// COMMAND_LOAD_AND_LAUNCH_MISSION_INTERNAL: false = the mission is not started.
bool MissionLaunchAllowed(void);
// CGame::Process, right after TheCamera.Process(): scripted camera and the
// hidden player that follows it (streaming, zones and population follow the
// player, not the camera).
void AfterCameraProcess(void);
// Idle, after the paused-or-not CGame::Process: camera for frozen tests.
void AfterGameProcess(void);
// Idle: after DoRWStuffStartOfFrame_Horizon (3D scope begins) and before
// Render2dStuff (3D scope ends). Viewport scale, draw modes, flat textures
// only apply between the two, so 2D stays intact.
void Begin3D(void);
void End3D(void);
// Render2dStuffAfterFade (main.cpp), before its CFont::DrawFonts(): title
// cards, progress and the final results screen.
void Render2D(void);
// DoRWStuffEndOfFrame (main.cpp), around RsCameraShowRaster.
void SwapBegin(void);
void SwapEnd(void);
// Main loops: true once the run is over and the user left the results screen
// (or aborted): set RsGlobal.quit.
bool QuitRequested(void);

} // namespace Bench

// ---- Feature gates -------------------------------------------------------------
// Read by render/simulation code at the gated call sites. All true (normal)
// except while an isolation test disables one subsystem.
struct BenchGates {
	bool sky;          // CClouds::RenderBackground + CClouds::Render + DoRWRenderHorizon
	bool reflections;  // CCoronas::RenderReflections (wet roads)
	bool shadows;      // CShadows::RenderStaticShadows + RenderStoredShadows
	bool coronas;      // CCoronas::Render + RenderSunReflection
	bool particles;    // CParticle::Render
	bool fxMisc;       // skidmarks, rubbish, glass, rain streaks, water cannons,
	                   // antennas, special FX, ropes, pickups, weapon FX,
	                   // fog lights, moving things (RenderEffects_new)
	bool envMap;       // CRenderer::GenerateEnvironmentMapBeforeFrame (PSP2), GenerateEnvironmentMap
	bool audio;        // DMAudio.Service()
	bool postfx;       // TheCamera.RenderMotionBlur() (colour filter pass)
	float farClip;     // > 0: overrides CTimeCycle::GetFarClip() in Idle
};
extern BenchGates gBenchGates;
#define BENCH_GATE(x) (gBenchGates.x)

#else

#define BENCH_GATE(x) (true)

#endif
