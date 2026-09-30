#ifdef RELCS_BENCHMARK

// reLCS Benchmark director: boot, test catalogue sequencing, scripted scenes,
// isolation switches, title cards and the results screen (see benchmark.h).

#include "common.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
#include "General.h"
#include "Timer.h"
#include "Pad.h"
#include "Game.h"
#include "Frontend.h"
#include "IniFile.h"
#include "Renderer.h"
#include "Camera.h"
#include "Draw.h"
#include "World.h"
#include "Pools.h"
#include "PlayerPed.h"
#include "PlayerInfo.h"
#include "Wanted.h"
#include "Streaming.h"
#include "Population.h"
#include "CarCtrl.h"
#include "PathFind.h"
#include "ModelIndices.h"
#include "Automobile.h"
#include "Zones.h"
#include "Clock.h"
#include "Weather.h"
#include "Hud.h"
#include "Messages.h"
#include "CutsceneMgr.h"
#include "Font.h"
#include "Sprite2d.h"
#include "Explosion.h"
#include "Fire.h"
#include "Particle.h"
#include "ParticleObject.h"
#include "Skidmarks.h"
#include "PedType.h"
#include "RwHelper.h"
#include "benchmark.h"
#include "benchmark_platform.h"
#include "benchmark_metrics.h"
#include "benchmark_scenes.h"

#ifdef NEW_RENDERER
extern bool gbRenderRoads;
extern bool gbRenderEverythingBarRoads;
extern bool gbRenderFadingInEntities;
extern bool gbRenderWater;
extern bool gbRenderBoats;
extern bool gbRenderVehicles;
extern bool gbRenderWorld0;
extern bool gbRenderWorld1;
extern bool gbRenderWorld2;
#endif

static const BenchGates kDefaultGates = { true, true, true, true, true, true, true, true, true, 0.0f };
BenchGates gBenchGates = { true, true, true, true, true, true, true, true, true, 0.0f };

#define BENCH_MAX_FRAMES 60000
#define BENCH_FIXED_DT (1.0f / 30.0f)
#define QUIET_FRAMES 32
#define SETTLE_MAX 8.0f
#define MAX_BENCH_CARS 8

enum {
	PH_OFF, PH_BOOT, PH_CARD, PH_SETTLE, PH_MEASURE, PH_WRITE, PH_RESULTS, PH_QUIT
};

enum { CAM_NONE, CAM_STATIC, CAM_PATH, CAM_FOLLOW };

// ---- Configuration (benchmark.ini, command line) -------------------------------

static struct {
	bool quick;
	char tests[256];
	int loops;
	bool autoExit;
	bool overlay;
	bool fixedStep;
	int seed;
	bool shots;     // PC only: screenshots of every test (scene validation)
} sCfg = { false, "", 1, false, false, true, 1234, false };

#ifdef PSP2
static bool sActive = true;
#else
static bool sActive = false;
#endif
static char sCmdTests[256];
static bool sInit;              // BenchMetrics ready
static BenchRun sRun;

// ---- Test list ----------------------------------------------------------------

struct BenchEntry { int16 scene, vp, var; };   // scene < 0: isolation vp x var
static BenchEntry sEntries[BENCH_MAX_TESTS];
static int sNumEntries;
static int sCur, sLoop;

// ---- Director state -------------------------------------------------------------

static int sPhase = PH_OFF;
static float sPhaseTime;        // real seconds in the phase
static int sPhaseFrames;
static uint64_t sLastUs, sFrameUs;
static float sFrameStep;        // benchmark time of this frame (1/30 or real, clamped)

static bool sScriptsFrozen, sMissionsBlocked;
static bool sQuit, sAbort, sWriteOk;
static uint32_t sBtn, sBtnPrev;
static uint64_t sComboUs;
static int sPage;

static bool sFixed;             // fixed time step wanted
static double sMsCarry;
static bool sNonClippedValid;
static uint32 sNonClipped;

static BenchTestResult *sTest;
static bool sInterrupted;
static int sSeedBase;
static int sHour, sWeather;
static float sDensity;
static bool sHud, sPausedTest, sGpuSync, sPump;
static float sSettleMin, sSettleMax, sBenchTime;
static int sQuietNeed, sSettleFrames, sQuiet, sMeasFrames, sTargetFrames;
static uint64_t sMeasStartUs, sQuietStartUs;
static double sLoadMs = -1.0, sSettleMs = -1.0;

static int sIsoVp = -1, sIsoStage;
static bool sIsoFrozen;

static int sCamMode = CAM_NONE;
static CVector sCamSrc, sCamDst;

static int sCarHandles[MAX_BENCH_CARS];
static int sNumCars;
static int sModels[4], sNumModels;
static bool sMayhem, sFiresStarted, sThreatsSaved;
static int sNextBlast;
static CVector sMayhemCentre;
static uint32 sSavedThreats[NUM_PEDTYPES];

static struct {
	bool w0, w1, w2, water, ebr, fading, boats, veh, roads;
	bool dontPeds, dontVeh, dontObj, ps2a, backface;
	float lod;
} sRender;

// ---- Small helpers -------------------------------------------------------------

static uint64_t
NowUs(void)
{
	return BenchPlatform::NowUs();
}

static bool
EqNoCase(const char *a, const char *b)
{
	for(; *a && *b; a++, b++)
		if(tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return false;
	return *a == *b;
}

static char *
TrimStr(char *s)
{
	while(*s == ' ' || *s == '\t') s++;
	char *e = s + strlen(s);
	while(e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
		*--e = '\0';
	return s;
}

static void
CopyStr(char *dst, const char *src, int size)
{
	strncpy(dst, src, size - 1);
	dst[size - 1] = '\0';
}

static void
SetPhase(int phase)
{
	sPhase = phase;
	sPhaseTime = 0.0f;
	sPhaseFrames = 0;
}

static bool
Pressed(uint32_t b)
{
	return (sBtn & b) && !(sBtnPrev & b);
}

// PC scene validation (Screenshots=1): PNGs next to the results, taken after
// the swap of the requested frame. Encoding is slow: never use for timings.
#ifndef PSP2
bool RwGrabScreen(RwCamera *camera, RwChar *filename);
static char sShotName[160];
static void
RequestShot(const char *id, const char *tag)
{
	if(!sCfg.shots || sShotName[0])
		return;
	char root[128];
	CopyStr(root, BenchPlatform::ResultsRoot(), sizeof(root));
	BenchPlatform::MakeDir(root);
	BenchPlatform::MakeDir(sRun.folder);
	snprintf(sShotName, sizeof(sShotName), "%sshot_%s_%s.png", sRun.folder, id, tag);
}
#endif

// ---- Settings -----------------------------------------------------------------

static void
LoadIni(void)
{
	static char buf[4096];
	if(BenchPlatform::ReadSettingsFile(buf, sizeof(buf)) < 0)
		return;
	bool inSection = false;
	char *line = buf;
	while(line && *line){
		char *next = strchr(line, '\n');
		if(next) *next++ = '\0';
		char *s = TrimStr(line);
		line = next;
		if(*s == '\0' || *s == ';' || *s == '#')
			continue;
		if(*s == '['){
			char *e = strchr(s, ']');
			if(e) *e = '\0';
			inSection = EqNoCase(TrimStr(s + 1), "Benchmark");
			continue;
		}
		char *eq = strchr(s, '=');
		if(!inSection || !eq)
			continue;
		*eq = '\0';
		char *key = TrimStr(s);
		char *val = TrimStr(eq + 1);
		if(EqNoCase(key, "Mode")) sCfg.quick = EqNoCase(val, "quick");
		else if(EqNoCase(key, "Tests")) CopyStr(sCfg.tests, val, sizeof(sCfg.tests));
		else if(EqNoCase(key, "Loops")) sCfg.loops = Clamp(atoi(val), 1, 10);
		else if(EqNoCase(key, "AutoExit")) sCfg.autoExit = atoi(val) != 0;
		else if(EqNoCase(key, "Overlay")) sCfg.overlay = atoi(val) != 0;
		else if(EqNoCase(key, "FixedStep")) sCfg.fixedStep = atoi(val) != 0;
		else if(EqNoCase(key, "Seed")) sCfg.seed = atoi(val);
		else if(EqNoCase(key, "Screenshots")) sCfg.shots = atoi(val) != 0;
	}
}

static const char *
TestList(void)
{
	return sCmdTests[0] ? sCmdTests : sCfg.tests;
}

// ---- Catalogue ----------------------------------------------------------------

static bool
IsRunningVariant(const BenchVariant &v)
{
	return v.action == BVA_SIM_RUNNING || v.action == BVA_NO_AUDIO;
}

static void
EntryId(const BenchEntry &e, int loop, char *id, int size)
{
	char tmp[64];
	if(e.scene >= 0)
		CopyStr(tmp, gBenchScenes[e.scene].id, sizeof(tmp));
	else
		sprintf(tmp, "iso_%c_%s", gBenchViewpoints[e.vp].id[0], gBenchVariants[e.var].id);
	if(loop > 0)
		sprintf(tmp + strlen(tmp), "_L%d", loop + 1);
	CopyStr(id, tmp, size);
}

static const char *
EntryGroup(const BenchEntry &e)
{
	return e.scene >= 0 ? gBenchScenes[e.scene].group : "isolation";
}

static void
EntryName(const BenchEntry &e, char *name, int size)
{
	char tmp[128];
	if(e.scene >= 0)
		CopyStr(tmp, gBenchScenes[e.scene].name, sizeof(tmp));
	else
		sprintf(tmp, "%s: %s", gBenchViewpoints[e.vp].name, gBenchVariants[e.var].name);
	CopyStr(name, tmp, size);
}

static bool
TokenMatches(const char *tok, const BenchEntry &e)
{
	char id[64];
	EntryId(e, 0, id, sizeof(id));
	int n = (int)strlen(tok);
	if(n > 0 && tok[n - 1] == '*')
		return strncmp(id, tok, n - 1) == 0;
	if(strcmp(tok, id) == 0 || strcmp(tok, EntryGroup(e)) == 0)
		return true;
	if(e.scene < 0){
		const char *vp = gBenchViewpoints[e.vp].id;
		if(strcmp(tok, vp) == 0 || strcmp(tok, gBenchVariants[e.var].id) == 0)
			return true;
		if(strncmp(tok, "iso_", 4) == 0 && (strcmp(tok + 4, vp) == 0 || (tok[4] == vp[0] && tok[5] == '\0')))
			return true;
	}
	return false;
}

static bool
EntrySelected(const BenchEntry &e, const char *list)
{
	if(list[0] == '\0'){
		if(!sCfg.quick)
			return true;
		if(e.scene >= 0)
			return gBenchScenes[e.scene].quick;
		return gBenchViewpoints[e.vp].quick && gBenchVariants[e.var].quick;
	}
	char buf[256];
	CopyStr(buf, list, sizeof(buf));
	for(char *tok = strtok(buf, ", ;\t"); tok; tok = strtok(nil, ", ;\t"))
		if(TokenMatches(tok, e))
			return true;
	return false;
}

static void
AddEntries(const char *list)
{
	sNumEntries = 0;
	for(int i = 0; i < gBenchNumScenes; i++){
		BenchEntry e = { (int16)i, -1, -1 };
		if(sNumEntries < BENCH_MAX_TESTS && EntrySelected(e, list))
			sEntries[sNumEntries++] = e;
	}
	for(int vp = 0; vp < gBenchNumViewpoints; vp++)
		for(int v = 0; v < gBenchNumVariants; v++){
			BenchEntry e = { -1, (int16)vp, (int16)v };
			if(sNumEntries < BENCH_MAX_TESTS && EntrySelected(e, list))
				sEntries[sNumEntries++] = e;
		}
}

static void
BuildEntries(void)
{
	AddEntries(TestList());
	if(sNumEntries == 0)
		AddEntries("");   // nothing matched: fall back to the mode
	// every loop adds a result per entry: stay within the result table
	if(sNumEntries > 0)
		sCfg.loops = Clamp(sCfg.loops, 1, Max(BENCH_MAX_TESTS / sNumEntries, 1));
}

static const BenchEntry *
NextEntry(void)
{
	if(sCur + 1 < sNumEntries) return &sEntries[sCur + 1];
	if(sLoop + 1 < sCfg.loops) return &sEntries[0];
	return nil;
}

static int
ProgressDone(void)
{
	return sLoop * sNumEntries + sCur;
}

static int
ProgressTotal(void)
{
	return Max(sCfg.loops * sNumEntries, 1);
}

// ---- Render switches ----------------------------------------------------------

static void
SaveRender(void)
{
#ifdef NEW_RENDERER
	sRender.w0 = gbRenderWorld0; sRender.w1 = gbRenderWorld1; sRender.w2 = gbRenderWorld2;
	sRender.water = gbRenderWater; sRender.ebr = gbRenderEverythingBarRoads;
	sRender.fading = gbRenderFadingInEntities; sRender.boats = gbRenderBoats;
	sRender.veh = gbRenderVehicles; sRender.roads = gbRenderRoads;
#endif
	sRender.dontPeds = gbDontRenderPeds; sRender.dontVeh = gbDontRenderVehicles;
	sRender.dontObj = gbDontRenderObjects;
	sRender.ps2a = gPS2alphaTest; sRender.backface = gBackfaceCulling;
	sRender.lod = CRenderer::ms_lodDistScale;
}

static void
RestoreRender(void)
{
#ifdef NEW_RENDERER
	gbRenderWorld0 = sRender.w0; gbRenderWorld1 = sRender.w1; gbRenderWorld2 = sRender.w2;
	gbRenderWater = sRender.water; gbRenderEverythingBarRoads = sRender.ebr;
	gbRenderFadingInEntities = sRender.fading; gbRenderBoats = sRender.boats;
	gbRenderVehicles = sRender.veh; gbRenderRoads = sRender.roads;
#endif
	gbDontRenderPeds = sRender.dontPeds; gbDontRenderVehicles = sRender.dontVeh;
	gbDontRenderObjects = sRender.dontObj;
	gPS2alphaTest = sRender.ps2a; gBackfaceCulling = sRender.backface;
	CRenderer::ms_lodDistScale = sRender.lod;
	gBenchGates = kDefaultGates;
	BenchPlatform::SetViewportScale(1.0f);
	BenchPlatform::SetDrawMode(BENCH_DRAW_NORMAL);
	BenchPlatform::SetFlatTextures(false);
	BenchPlatform::SetGpuSync(false);
	sGpuSync = false;
}

// Returns false (and a reason) when the variant cannot run here
static bool
ApplyVariant(const BenchVariant &v, const char **reason)
{
	*reason = "non supporte par cette plateforme";
	if(v.cap && !(BenchPlatform::Caps() & v.cap))
		return false;
	switch(v.action){
	case BVA_NO_PEDS: gbDontRenderPeds = true; break;
	case BVA_NO_VEHICLES: gbDontRenderVehicles = true; break;
	case BVA_NO_OBJECTS:
		gbDontRenderObjects = true;
#ifdef NEW_RENDERER
		gbRenderEverythingBarRoads = false;
#endif
		break;
#ifdef NEW_RENDERER
	case BVA_NO_WORLD_LOD: gbRenderWorld0 = false; break;
	case BVA_NO_WORLD_OPAQUE: gbRenderWorld1 = false; break;
	case BVA_NO_WORLD_TRANSPARENT: gbRenderWorld2 = false; break;
	case BVA_NO_WATER: gbRenderWater = false; break;
	case BVA_NO_RENDER_3D:
		gbRenderWorld0 = gbRenderWorld1 = gbRenderWorld2 = false;
		gbRenderWater = gbRenderEverythingBarRoads = gbRenderFadingInEntities = false;
		gbRenderBoats = gbRenderVehicles = gbRenderRoads = false;
		gBenchGates.sky = gBenchGates.reflections = gBenchGates.shadows = false;
		gBenchGates.coronas = gBenchGates.particles = gBenchGates.fxMisc = false;
		gBenchGates.envMap = gBenchGates.postfx = false;
		break;
#else
	case BVA_NO_WORLD_LOD: case BVA_NO_WORLD_OPAQUE: case BVA_NO_WORLD_TRANSPARENT:
	case BVA_NO_WATER: case BVA_NO_RENDER_3D:
		*reason = "rendu NEW_RENDERER absent";
		return false;
#endif
	case BVA_NO_SKY: gBenchGates.sky = false; break;
	case BVA_NO_SHADOWS: gBenchGates.shadows = false; break;
	case BVA_NO_CORONAS: gBenchGates.coronas = false; gBenchGates.reflections = false; break;
	case BVA_NO_PARTICLES_FX: gBenchGates.particles = false; gBenchGates.fxMisc = false; break;
	case BVA_NO_ENVMAP: gBenchGates.envMap = false; break;
	case BVA_NO_POSTFX: gBenchGates.postfx = false; break;
	case BVA_HUD_ON: sHud = true; break;
	case BVA_PS2_ALPHA_OFF: gPS2alphaTest = false; break;
	case BVA_BACKFACE_CULL: gBackfaceCulling = true; break;
	case BVA_LOD: CRenderer::ms_lodDistScale = v.value; break;
	case BVA_FARCLIP: gBenchGates.farClip = v.value; break;
	case BVA_VIEWPORT: if(!BenchPlatform::SetViewportScale(v.value)) return false; break;
	case BVA_FLAT_TEXTURES: if(!BenchPlatform::SetFlatTextures(true)) return false; break;
	case BVA_DRAW_TINY: if(!BenchPlatform::SetDrawMode(BENCH_DRAW_TINY)) return false; break;
	case BVA_DRAW_NULL: if(!BenchPlatform::SetDrawMode(BENCH_DRAW_NULL)) return false; break;
	case BVA_GPU_SYNC: BenchPlatform::SetGpuSync(true); sGpuSync = true; break;
	case BVA_NO_AUDIO: gBenchGates.audio = false; break;
	default: break;
	}
	return true;
}

// ---- World helpers ---------------------------------------------------------------

static void
ForceFadeIn(void)
{
	TheCamera.Fade(0.0f, FADE_IN);
	TheCamera.m_fFLOATingFade = 0.0f;
	TheCamera.m_bFading = false;
	CDraw::FadeValue = 0;
}

static void
HidePlayer(void)
{
	CPlayerPed *p = FindPlayerPed();
	if(p == nil)
		return;
	p->bIsVisible = false;
	p->bUsesCollision = false;
	p->bDrownsInWater = false;
	p->RemoveFromMovingList();
	p->bIsStaticWaitingForCollision = true;
	p->SetMoveSpeed(0.0f, 0.0f, 0.0f);
}

// Streaming, collision, zones and population follow the player, not the camera
static void
TeleportPlayer(const CVector &pos)
{
	CPlayerPed *p = FindPlayerPed();
	if(p == nil || p->InVehicle())
		return;
	if((p->GetPosition() - pos).MagnitudeSqr() > 0.01f)
		p->Teleport(pos);
}

static void
TakePlayerOutOfCar(CPlayerPed *p)
{
	CVehicle *v = p->m_pMyVehicle;
	if(v->pDriver == p){
		v->RemoveDriver();
		v->SetStatus(STATUS_ABANDONED);
		v->bEngineOn = false;
	}else
		v->RemovePassenger(p);
	p->RemoveInCarAnims();
	p->bInVehicle = false;
	p->m_pMyVehicle = nil;
	p->m_pVehicleAnim = nil;
	p->SetPedState(PED_IDLE);
}

static void
SetDensity(float d)
{
	float cap = Max(d, 1.0f);
	CPopulation::PedDensityMultiplier = d;
	CCarCtrl::CarDensityMultiplier = d;
	CPopulation::MaxNumberOfPedsInUse = DEFAULT_MAX_NUMBER_OF_PEDS * CIniFile::PedNumberMultiplier * cap;
	CPopulation::MaxNumberOfPedsInUseInterior = DEFAULT_MAX_NUMBER_OF_PEDS_INTERIOR * CIniFile::PedNumberMultiplier * cap;
	CCarCtrl::MaxNumberOfCarsInUse = DEFAULT_MAX_NUMBER_OF_CARS * CIniFile::CarNumberMultiplier * cap;
}

static void
ResetEffects(void)
{
	CExplosion::ClearAllExplosions();
	CParticleObject::RemoveAllExpireableParticleObjects();
	for(int t = PARTICLE_FIRST; t <= PARTICLE_LAST; t++)
		CParticle::RemovePSystem((tParticleType)t);
	CSkidmarks::Clear();
	CMessages::ClearMessages();
	CHud::GetRidOfAllHudMessages();
}

static CVehicle *
BenchCar(int i)
{
	int h = sCarHandles[i];
	return h >= 0 ? CPools::GetVehiclePool()->GetAt(h) : nil;
}

static void
DeleteBenchCars(void)
{
	for(int i = 0; i < sNumCars; i++){
		CVehicle *v = BenchCar(i);
		sCarHandles[i] = -1;
		if(v == nil || v == FindPlayerVehicle())
			continue;
		CCarCtrl::RemoveFromInterestingVehicleList(v);
		CWorld::Remove(v);
		CWorld::RemoveReferencesToDeletedObject(v);
		delete v;
	}
	sNumCars = 0;
}

static CVehicle *
NewBenchCar(int mi)
{
	if(sNumCars >= MAX_BENCH_CARS)
		return nil;
	CVehicle *v = new CAutomobile(mi, MISSION_VEHICLE);
	if(v)
		sCarHandles[sNumCars++] = CPools::GetVehiclePool()->GetIndex(v);
	return v;
}

// Loads the first available models (kept until ReleaseCarModels)
static int
LoadCarModels(const int *ids, int n)
{
	CTimer::Suspend();
	for(int i = 0; i < n; i++)
		CStreaming::RequestModel(ids[i], STREAMFLAGS_DONT_REMOVE);
	CStreaming::LoadAllRequestedModels(false);
	CTimer::Resume();
	sNumModels = 0;
	for(int i = 0; i < n && sNumModels < (int)ARRAY_SIZE(sModels); i++)
		if(CStreaming::HasModelLoaded(ids[i]))
			sModels[sNumModels++] = ids[i];
		else
			CStreaming::SetModelIsDeletable(ids[i]);
	return sNumModels;
}

static void
ReleaseCarModels(void)
{
	for(int i = 0; i < sNumModels; i++)
		CStreaming::SetModelIsDeletable(sModels[i]);
	sNumModels = 0;
}

static void
SetClockWeather(int hour, int weather)
{
	sHour = hour;
	sWeather = weather;
	CClock::SetGameClock(hour, 0);
	CWeather::ForceWeatherNow(weather);
}

// Clears the world, loads the scene around `pos` and puts the hidden player there
static void
PreparePlace(const CVector &pos, int hour, int weather, float density, double *loadMs)
{
	CTimer::SetCodePause(false);
	DeleteBenchCars();
	CTimer::Suspend();
	CWorld::ClearExcitingStuffFromArea(pos, 5000.0f, true);
	ResetEffects();
	TeleportPlayer(pos);
	HidePlayer();
	uint64_t t0 = NowUs();
	CStreaming::LoadScene(pos);
	CStreaming::LoadSceneCollision(pos);
	if(loadMs)
		*loadMs = (NowUs() - t0) / 1000.0;
	CTimer::Resume();
	SetClockWeather(hour, weather);
	sDensity = density;
	SetDensity(density);
	CCarCtrl::CountDownToCarsAtStart = 1;
	CPopulation::m_CountDownToPedsAtStart = 1;
	CGeneral::SetRandomSeed(sSeedBase);
	ForceFadeIn();
}

// ---- Camera -----------------------------------------------------------------------

static void
ApplyCamera(const CVector &src, const CVector &dst)
{
	if(TheCamera.m_pRwCamera == nil)
		return;
	CVector front = dst - src;
	if(front.MagnitudeSqr() < 0.0001f)
		front = CVector(0.0f, 1.0f, 0.0f);
	front.Normalise();
	CVector up(0.0f, 0.0f, 1.0f);
	CVector right = CrossProduct(front, up);
	if(right.MagnitudeSqr() < 0.0001f)
		right = CVector(1.0f, 0.0f, 0.0f);
	right.Normalise();
	up = CrossProduct(right, front);
	up.Normalise();

	CCam &cam = TheCamera.Cams[TheCamera.ActiveCam];
	cam.m_cvecCamFixedModeSource = src;
	cam.m_cvecCamFixedModeVector = dst;

	CMatrix &m = TheCamera.GetMatrix();
	m.GetRight() = CrossProduct(up, front);	// actually left, like CCamera::Process
	m.GetForward() = front;
	m.GetUp() = up;
	m.GetPosition() = src;

	RwCameraSetNearClipPlane(TheCamera.m_pRwCamera, DEFAULT_NEAR);
	CDraw::SetFOV(70.0f);
	TheCamera.CalculateDerivedValues();
	TheCamera.GetGameCamPosition() = src;

	RwFrame *frame = RwCameraGetFrame(TheCamera.m_pRwCamera);
	*RwMatrixGetPos(RwFrameGetMatrix(frame)) = TheCamera.GetPosition();
	*RwMatrixGetAt(RwFrameGetMatrix(frame)) = TheCamera.GetForward();
	*RwMatrixGetUp(RwFrameGetMatrix(frame)) = TheCamera.GetUp();
	*RwMatrixGetRight(RwFrameGetMatrix(frame)) = TheCamera.GetRight();
	RwMatrixUpdate(RwFrameGetMatrix(frame));
	RwFrameUpdateObjects(frame);
	RwFrameOrthoNormalize(frame);

	TheCamera.LODDistMultiplier = Min(70.0f / CDraw::GetFOV(), 2.2f);
	TheCamera.GenerationDistMultiplier = TheCamera.LODDistMultiplier;
	TheCamera.LODDistMultiplier *= CRenderer::ms_lodDistScale;
	CDraw::SetNearClipZ(RwCameraGetNearClipPlane(TheCamera.m_pRwCamera));
	CDraw::SetFarClipZ(RwCameraGetFarClipPlane(TheCamera.m_pRwCamera));
}

// Script fixed mode: no follow-cam line of sight or collision logic underneath
static void
SetScriptCamera(const CVector &src, const CVector &dst)
{
	sCamSrc = src;
	sCamDst = dst;
	TheCamera.SetCamPositionForFixedMode(src, CVector(0.0f, 0.0f, 0.0f));
	TheCamera.TakeControlNoEntity(dst, JUMP_CUT, CAMCONTROL_SCRIPT);
	ApplyCamera(src, dst);
}

// Keeps the current view still (between tests)
static void
HoldCamera(void)
{
	if(sCamMode == CAM_NONE)
		return;
	if(sCamMode == CAM_FOLLOW){
		sCamSrc = TheCamera.GetPosition();
		sCamDst = sCamSrc + TheCamera.GetForward() * 10.0f;
	}
	sCamMode = CAM_STATIC;
	SetScriptCamera(sCamSrc, sCamDst);
}

// ---- Camera paths (centripetal Catmull-Rom, constant speed by arc length) ---------

#define PATH_MAX_KEYS 160
#define PATH_SUB 8
static CVector sKeys[PATH_MAX_KEYS];
static int sNumKeys;
static float sArc[(PATH_MAX_KEYS - 1) * PATH_SUB + 1];
static int sNumArc;
static float sPathLen, sPathDist, sPathSpeed;
static const BenchScene *sPathScene;

static CVector
PathKey(int i)
{
	if(i < 0) return sKeys[0] * 2.0f - sKeys[1];
	if(i >= sNumKeys) return sKeys[sNumKeys - 1] * 2.0f - sKeys[sNumKeys - 2];
	return sKeys[i];
}

static float
PathKnot(const CVector &a, const CVector &b)
{
	return Max(Sqrt((b - a).Magnitude()), 0.01f);
}

static CVector
PathLerp(const CVector &a, const CVector &b, float ta, float tb, float t)
{
	float d = tb - ta;
	return a * ((tb - t) / d) + b * ((t - ta) / d);
}

static CVector
SplinePoint(int seg, float u)
{
	CVector p0 = PathKey(seg - 1), p1 = PathKey(seg), p2 = PathKey(seg + 1), p3 = PathKey(seg + 2);
	float t0 = 0.0f;
	float t1 = t0 + PathKnot(p0, p1);
	float t2 = t1 + PathKnot(p1, p2);
	float t3 = t2 + PathKnot(p2, p3);
	float t = t1 + (t2 - t1) * u;
	CVector a1 = PathLerp(p0, p1, t0, t1, t);
	CVector a2 = PathLerp(p1, p2, t1, t2, t);
	CVector a3 = PathLerp(p2, p3, t2, t3, t);
	CVector b1 = PathLerp(a1, a2, t0, t2, t);
	CVector b2 = PathLerp(a2, a3, t1, t3, t);
	return PathLerp(b1, b2, t1, t2, t);
}

static void
BuildArc(void)
{
	sNumArc = 1;
	sArc[0] = 0.0f;
	sPathLen = 0.0f;
	if(sNumKeys < 2)
		return;
	CVector prev = sKeys[0];
	for(int s = 0; s < sNumKeys - 1; s++)
		for(int k = 1; k <= PATH_SUB; k++){
			CVector p = SplinePoint(s, (float)k / PATH_SUB);
			sArc[sNumArc] = sArc[sNumArc - 1] + (p - prev).Magnitude();
			sNumArc++;
			prev = p;
		}
	sPathLen = sArc[sNumArc - 1];
}

static CVector
PathPoint(float d)
{
	if(sNumKeys < 2 || d <= 0.0f) return sKeys[0];
	if(d >= sPathLen) return sKeys[sNumKeys - 1];
	int lo = 0, hi = sNumArc - 1;
	while(hi - lo > 1){
		int mid = (lo + hi) / 2;
		if(sArc[mid] <= d) lo = mid;
		else hi = mid;
	}
	float span = sArc[hi] - sArc[lo];
	float u = (lo + (span > 0.0f ? (d - sArc[lo]) / span : 0.0f)) / PATH_SUB;
	int seg = (int)u;
	if(seg >= sNumKeys - 1)
		return sKeys[sNumKeys - 1];
	return SplinePoint(seg, u - seg);
}

// Past the end: continue along the last direction (look-ahead target)
static CVector
PathPointExt(float d)
{
	if(d <= sPathLen || sNumKeys < 2)
		return PathPoint(d);
	CVector end = PathPoint(sPathLen);
	CVector dir = end - PathPoint(sPathLen - 2.0f);
	dir.Normalise();
	return end + dir * (d - sPathLen);
}

static void
SetupPath(const BenchScene &s)
{
	sPathScene = &s;
	sNumKeys = Min(s.numPath, PATH_MAX_KEYS);
	for(int i = 0; i < sNumKeys; i++)
		sKeys[i] = CVector(s.path[i].x, s.path[i].y, s.path[i].z + (s.groundPath ? 0.0f : s.camHeight));
	BuildArc();
}

// Resamples the path every ~10 m on the collision ground (collision must be loaded)
static void
GroundPath(const BenchScene &s)
{
	static CVector pts[PATH_MAX_KEYS];
	float step = Max(10.0f, sPathLen / (PATH_MAX_KEYS - 1));
	int n = Clamp((int)(sPathLen / step) + 1, 2, PATH_MAX_KEYS);
	for(int i = 0; i < n; i++){
		CVector p = PathPoint(i == n - 1 ? sPathLen : i * step);
		bool found = false;
		float g = CWorld::FindGroundZFor3DCoord(p.x, p.y, p.z + 5.0f, &found);
		if(found && Abs(g - p.z) < 6.0f)
			p.z = g;
		pts[i] = p;
	}
	for(int i = 0; i < n; i++){
		float z = pts[i].z;
		if(i > 0 && i < n - 1)
			z = (pts[i - 1].z + 2.0f * pts[i].z + pts[i + 1].z) * 0.25f;
		sKeys[i] = CVector(pts[i].x, pts[i].y, z + s.camHeight);
	}
	sNumKeys = n;
	BuildArc();
}

static void
PathCamera(float d, CVector *src, CVector *dst)
{
	const BenchScene &s = *sPathScene;
	*src = PathPoint(d);
	CVector ahead = PathPointExt(d + s.lookAhead);
	ahead.z -= s.lookDrop;
	*dst = ahead;
	if(s.lookUntil > 0.0f && sPathLen > 0.0f){
		CVector look(s.look.x, s.look.y, s.look.z);
		float f = d / sPathLen;
		if(f <= s.lookUntil)
			*dst = look;
		else if(f < s.lookBlendEnd)
			*dst = look + (ahead - look) * ((f - s.lookUntil) / (s.lookBlendEnd - s.lookUntil));
	}
}

static void
UpdateCamera(void)
{
	switch(sCamMode){
	case CAM_PATH:
		PathCamera(sPathDist, &sCamSrc, &sCamDst);
		ApplyCamera(sCamSrc, sCamDst);
		TeleportPlayer(sCamSrc);
		break;
	case CAM_STATIC:
		ApplyCamera(sCamSrc, sCamDst);
		TeleportPlayer(sCamSrc);
		break;
	case CAM_FOLLOW: {
		CVehicle *v = BenchCar(0);
		if(v)
			TeleportPlayer(v->GetPosition() + CVector(0.0f, 0.0f, 4.0f));
		break;
	}
	}
}

// ---- Test lifecycle -----------------------------------------------------------------

static void StartWrite(void);
static void NextTest(void);

static BenchTestResult *
BeginResult(void)
{
	const BenchEntry &e = sEntries[sCur];
	char id[32], name[64];
	EntryId(e, sLoop, id, sizeof(id));
	EntryName(e, name, sizeof(name));
	int kind = e.scene >= 0 ? gBenchScenes[e.scene].kind : BTK_STATIC;
	BenchTestResult *t = BenchMetrics::BeginTest(id, EntryGroup(e), name, kind);
	if(t == nil)
		return nil;
	if(e.scene < 0){
		CopyStr(t->variant, gBenchVariants[e.var].id, sizeof(t->variant));
		CopyStr(t->viewpoint, gBenchViewpoints[e.vp].id, sizeof(t->viewpoint));
	}
	t->hour = sHour;
	t->minute = 0;
	t->weather = sWeather;
	t->pedDensity = sDensity;
	t->carDensity = sDensity;
	t->fixedStep = sFixed;
	t->paused = sPausedTest;
	t->loadMs = sLoadMs;
	t->settleMs = sSettleMs;
	return t;
}

static void
EndCleanup(void)
{
	const BenchEntry &e = sEntries[sCur];
	RestoreRender();
	sPump = false;
	HoldCamera();	// before deleting the followed car
	if(sThreatsSaved){
		for(int i = 0; i < NUM_PEDTYPES; i++)
			CPedType::SetThreats(i, sSavedThreats[i]);
		sThreatsSaved = false;
	}
	if(sMayhem){
		CExplosion::ClearAllExplosions();
		gFireManager.ExtinguishPoint(sMayhemCentre, 300.0f);
		CParticleObject::RemoveAllExpireableParticleObjects();
		sMayhem = false;
	}
	DeleteBenchCars();
	ReleaseCarModels();
	sFixed = false;
	sLoadMs = sSettleMs = -1.0;

	// the next variant of the same viewpoint reuses the frozen scene
	const BenchEntry *next = NextEntry();
	bool keep = e.scene < 0 && next && next->scene < 0 && next->vp == e.vp;
	if(!keep){
		CTimer::SetCodePause(false);
		sIsoVp = -1;
		sIsoFrozen = false;
	}
}

static void
SkipCurrent(const char *reason)
{
	BenchTestResult *t = BeginResult();
	if(t)
		BenchMetrics::SkipTest(t, reason);
	EndCleanup();
	if(t)
		NextTest();
	else
		StartWrite();
}

static void
EnterSettle(float minTime, float maxTime, int quiet, int frames)
{
	sSettleMin = minTime;
	sSettleMax = maxTime;
	sQuietNeed = quiet;
	sSettleFrames = frames;
	sQuiet = 0;
	sBenchTime = 0.0f;
	SetPhase(PH_SETTLE);
}

static void
StartMeasure(void)
{
	if(sTest == nil)	// teleports create it before their load
		sTest = BeginResult();
	if(sTest == nil){
		EndCleanup();
		StartWrite();
		return;
	}
	const BenchEntry &e = sEntries[sCur];
	sTargetFrames = 0;
	if(e.scene < 0)
		sTargetFrames = BENCH_ISO_FRAMES;
	else if(gBenchScenes[e.scene].frames > 0)
		sTargetFrames = gBenchScenes[e.scene].frames;
	else if(gBenchScenes[e.scene].setup != BSET_TELEPORT && gBenchScenes[e.scene].seconds > 0.0f && sFixed)
		sTargetFrames = (int)(gBenchScenes[e.scene].seconds * 30.0f + 0.5f);
	sInterrupted = false;
	sMeasFrames = 0;
	sBenchTime = 0.0f;
	sQuiet = 0;
	sMeasStartUs = NowUs();
	if(!sPausedTest)
		CGeneral::SetRandomSeed(sSeedBase);
	BenchPlatform::LogWindowBegin();
	BenchMetrics::StartMeasuring();
	SetPhase(PH_MEASURE);
}

static void
FinishTest(void)
{
	BenchMetrics::StopMeasuring();
	BenchPlatform::LogWindowEnd(sTest->id);
	sTest->loadMs = sLoadMs;
	sTest->settleMs = sSettleMs;
	if(sInterrupted) sTest->interrupted = true;
	BenchMetrics::EndTest();
	if(sInterrupted) sTest->interrupted = true;
	sTest = nil;
	EndCleanup();
	BenchMetrics::WritePartial();
	NextTest();
}

static void
NextTest(void)
{
	if(++sCur >= sNumEntries){
		sCur = 0;
		sLoop++;
	}
	if(sLoop >= sCfg.loops)
		StartWrite();
	else
		SetPhase(PH_CARD);
}

static void
StartWrite(void)
{
	sFixed = false;
	sPump = false;
	sHud = false;
	CHud::m_Wants_To_Draw_Hud = false;
	CTimer::SetCodePause(true);
	SetPhase(PH_WRITE);
}

static void
Abort(void)
{
	sRun.aborted = true;
	if(sPhase == PH_MEASURE && sTest){
		BenchMetrics::StopMeasuring();
		BenchPlatform::LogWindowEnd(sTest->id);
		sTest->interrupted = true;
		BenchMetrics::EndTest();
		sTest->interrupted = true;
		sTest = nil;
	}
	if(sPhase >= PH_CARD && sPhase <= PH_MEASURE)
		EndCleanup();
	StartWrite();
}

// ---- Scene preparation (one frame, blocking work allowed) ----------------------------

static void
PreparePath(const BenchScene &s)
{
	SetupPath(s);
	if(sNumKeys < 2){
		SkipCurrent("chemin invalide");
		return;
	}
	PreparePlace(sKeys[0], s.hour, s.weather, s.density, nil);
	if(s.groundPath)
		GroundPath(s);
	sPathSpeed = s.speed > 0.0f ? s.speed : (s.seconds > 0.0f ? sPathLen / s.seconds : 20.0f);
	sPathDist = 0.0f;
	sCamMode = CAM_PATH;
	CVector src, dst;
	PathCamera(0.0f, &src, &dst);
	SetScriptCamera(src, dst);
	TeleportPlayer(src);
	EnterSettle(s.settleMin, SETTLE_MAX, QUIET_FRAMES, 0);
}

static void
PrepareStatic(const BenchScene &s)
{
	CVector cam(s.path[0].x, s.path[0].y, s.path[0].z);
	PreparePlace(cam, s.hour, s.weather, s.density, nil);
	sCamMode = CAM_STATIC;
	SetScriptCamera(cam, CVector(s.look.x, s.look.y, s.look.z));
	EnterSettle(s.settleMin, SETTLE_MAX, QUIET_FRAMES, 0);
}

static void
PrepareFollow(const BenchScene &s)
{
	static const int kModels[] = { MI_TAXI, MI_KURUMA, MI_SENTINEL };
	int node = ThePaths.FindNodeClosestToCoors(CVector(s.path[0].x, s.path[0].y, s.path[0].z), PATH_CAR, 50.0f);
	if(node < 0){
		SkipCurrent("aucun noeud de route");
		return;
	}
	CVector pos = ThePaths.m_pathNodes[node].GetPosition();
	PreparePlace(pos + CVector(0.0f, 0.0f, 4.0f), s.hour, s.weather, s.density, nil);
	if(LoadCarModels(kModels, ARRAY_SIZE(kModels)) == 0){
		SkipCurrent("modele de vehicule non charge");
		return;
	}
	if(CPools::GetPedPool()->GetNoOfFreeSpaces() < 4){
		SkipCurrent("pool de pietons plein");
		return;
	}
	CVehicle *v = NewBenchCar(sModels[0]);
	if(v == nil){
		SkipCurrent("pool de vehicules plein");
		return;
	}
	pos.z += v->GetDistanceFromCentreOfMassToBaseOfModel();
	v->SetPosition(pos);
	v->SetHeading(DEGTORAD(ThePaths.FindNodeOrientationForCarPlacement(node)));
	v->SetStatus(STATUS_ABANDONED);
	v->bIsLocked = true;
	v->m_nZoneLevel = CTheZones::GetLevelFromPosition(&pos);
	CWorld::Add(v);
	// SetUpDriver only creates a driver for random vehicles
	v->VehicleCreatedBy = RANDOM_VEHICLE;
	v->SetUpDriver();
	v->VehicleCreatedBy = MISSION_VEHICLE;
	CCarCtrl::JoinCarWithRoadSystem(v);
	v->SetStatus(STATUS_PHYSICS);
	v->bEngineOn = true;
	v->AutoPilot.m_nCarMission = MISSION_CRUISE;
	v->AutoPilot.m_nTempAction = TEMPACT_NONE;
	v->AutoPilot.m_nDrivingStyle = DRIVINGSTYLE_AVOID_CARS;
	v->AutoPilot.m_nCruiseSpeed = 14;
	v->AutoPilot.m_fMaxTrafficSpeed = 14.0f;
	v->AutoPilot.m_nAntiReverseTimer = CTimer::GetTimeInMilliseconds();
	TheCamera.TakeControl(v, CCam::MODE_CAM_ON_A_STRING, JUMP_CUT, CAMCONTROL_SCRIPT);
	sCamMode = CAM_FOLLOW;
	EnterSettle(s.settleMin, SETTLE_MAX, QUIET_FRAMES, 0);
}

static void
PrepareMayhem(const BenchScene &s)
{
	static const int kModels[] = { MI_KURUMA, MI_SENTINEL, MI_TAXI };
	CVector want(s.path[0].x, s.path[0].y, s.path[0].z);
	int node = ThePaths.FindNodeClosestToCoors(want, PATH_CAR, 50.0f);
	CVector centre = node >= 0 ? ThePaths.m_pathNodes[node].GetPosition() : want;
	float heading = node >= 0 ? DEGTORAD(ThePaths.FindNodeOrientationForCarPlacement(node)) : 0.0f;
	// camera 30 m back along the road, 12 m up
	CVector cam = centre - CVector(-Sin(heading), Cos(heading), 0.0f) * 30.0f + CVector(0.0f, 0.0f, 12.0f);
	PreparePlace(cam, s.hour, s.weather, s.density, nil);
	if(LoadCarModels(kModels, ARRAY_SIZE(kModels)) == 0){
		SkipCurrent("modele de vehicule non charge");
		return;
	}
	for(int i = 0; i < MAX_BENCH_CARS; i++){
		float a = i * TWOPI / MAX_BENCH_CARS;
		CVector p = centre + CVector(Cos(a) * 12.0f, Sin(a) * 12.0f, 0.0f);
		bool found = false;
		float g = CWorld::FindGroundZFor3DCoord(p.x, p.y, centre.z + 4.0f, &found);
		p.z = found && Abs(g - centre.z) < 6.0f ? g : centre.z;
		CVehicle *v = NewBenchCar(sModels[i % sNumModels]);
		if(v == nil)
			break;
		p.z += v->GetDistanceFromCentreOfMassToBaseOfModel();
		v->SetPosition(p);
		v->SetHeading(a);
		v->SetStatus(STATUS_ABANDONED);
		v->bIsLocked = true;
		v->bEngineOn = false;
		v->bCanBeDamaged = true;
		v->m_nZoneLevel = CTheZones::GetLevelFromPosition(&p);
		CWorld::Add(v);
	}
	if(sNumCars == 0){
		SkipCurrent("pool de vehicules plein");
		return;
	}
	// riot: every civilian, gang and cop type hates the others (not the player)
	for(int i = 0; i < NUM_PEDTYPES; i++)
		sSavedThreats[i] = CPedType::GetThreats(i);
	sThreatsSaved = true;
	for(int i = PEDTYPE_CIVMALE; i < PEDTYPE_SPECIAL; i++)
		CPedType::SetThreats(i, PED_FLAG_CIVMALE | PED_FLAG_CIVFEMALE | PED_FLAG_COP | PED_FLAG_GANG1 |
			PED_FLAG_GANG2 | PED_FLAG_GANG3 | PED_FLAG_GANG4 | PED_FLAG_GANG5 | PED_FLAG_GANG6 |
			PED_FLAG_GANG7 | PED_FLAG_GANG8 | PED_FLAG_GANG9 | PED_FLAG_EMERGENCY |
			PED_FLAG_PROSTITUTE | PED_FLAG_CRIMINAL | PED_FLAG_SPECIAL);
	sMayhem = true;
	sMayhemCentre = centre;
	sNextBlast = 0;
	sFiresStarted = false;
	sCamMode = CAM_STATIC;
	SetScriptCamera(cam, centre);
	EnterSettle(s.settleMin, SETTLE_MAX, QUIET_FRAMES, 0);
}

static void
MayhemStep(void)
{
	if(!sFiresStarted){
		for(int i = 0; i < 4; i++){
			float a = (i + 0.5f) * HALFPI;
			gFireManager.StartFire(sMayhemCentre + CVector(Cos(a) * 7.0f, Sin(a) * 7.0f, 0.0f), 1.0f, 1);
		}
		sFiresStarted = true;
	}
	while(sNextBlast < sNumCars && sBenchTime >= 2.0f + 1.5f * sNextBlast){
		CVehicle *v = BenchCar(sNextBlast++);
		if(v && v->GetStatus() != STATUS_WRECKED){
			v->bCanBeDamaged = true;
			v->BlowUpCar(nil);
		}
	}
}

static void
PrepareTeleport(const BenchScene &s)
{
	CVector dest(s.path[0].x, s.path[0].y, s.path[0].z);
	// the test starts before the load so its totals include LoadScene
	sTest = BeginResult();
	if(sTest == nil){
		EndCleanup();
		StartWrite();
		return;
	}
	PreparePlace(dest, s.hour, s.weather, s.density, &sLoadMs);
	sTest->hour = sHour;
	sTest->weather = sWeather;
	sTest->pedDensity = sTest->carDensity = sDensity;
	sCamMode = CAM_STATIC;
	SetScriptCamera(dest, CVector(s.look.x, s.look.y, s.look.z));
	StartMeasure();	// the streaming settle is the measurement
}

static void
IsoApplyVariant(void)
{
	const BenchVariant &v = gBenchVariants[sEntries[sCur].var];
	bool running = IsRunningVariant(v);
	sIsoStage = 1;
	if(running){
		CTimer::SetCodePause(false);
		sIsoFrozen = false;
	}
	const char *reason;
	if(!ApplyVariant(v, &reason)){
		SkipCurrent(reason);
		return;
	}
	sPausedTest = !running;
	if(v.action == BVA_LOD){
		// streaming stops while paused: pump it without running the simulation
		sPump = true;
		EnterSettle(0.5f, 4.0f, 16, 0);
	}else if(running)
		EnterSettle(1.0f, 1.0f, 0, 0);
	else
		EnterSettle(0.0f, 0.0f, 0, 12);
}

static void
PrepareIso(const BenchEntry &e)
{
	const BenchViewpoint &vp = gBenchViewpoints[e.vp];
	const BenchVariant &v = gBenchVariants[e.var];
	sHud = false;
	sHour = 12;
	sWeather = WEATHER_SUNNY;
	sDensity = 1.0f;
	sFixed = sCfg.fixedStep;
	sPausedTest = !IsRunningVariant(v);
	if(v.cap && !(BenchPlatform::Caps() & v.cap)){
		SkipCurrent("non supporte par cette plateforme");
		return;
	}
	if(sIsoVp != e.vp || (!IsRunningVariant(v) && !sIsoFrozen)){
		CVector cam(vp.cam.x, vp.cam.y, vp.cam.z);
		PreparePlace(cam, 12, WEATHER_SUNNY, 1.0f, nil);
		sCamMode = CAM_STATIC;
		SetScriptCamera(cam, CVector(vp.look.x, vp.look.y, vp.look.z));
		sIsoVp = e.vp;
		sIsoFrozen = false;
		sIsoStage = 0;
		EnterSettle(3.0f, SETTLE_MAX, QUIET_FRAMES, 0);
	}else
		IsoApplyVariant();
}

static void
Prepare(void)
{
	const BenchEntry &e = sEntries[sCur];
	sSeedBase = sCfg.seed + sCur;
	sLoadMs = sSettleMs = -1.0;
	sGpuSync = false;
	if(e.scene < 0){
		PrepareIso(e);
		return;
	}
	const BenchScene &s = gBenchScenes[e.scene];
	sHud = s.hud;
	sFixed = sCfg.fixedStep && !s.realTime;
	sPausedTest = false;
	switch(s.setup){
	case BSET_PATH: PreparePath(s); break;
	case BSET_CROWD: PrepareStatic(s); break;
	case BSET_FOLLOW: PrepareFollow(s); break;
	case BSET_MAYHEM: PrepareMayhem(s); break;
	case BSET_TELEPORT: PrepareTeleport(s); break;
	default: SkipCurrent("type de scene inconnu"); break;
	}
}

// ---- Per-phase steps ----------------------------------------------------------------

// CStreaming::Update purges non-priority requests at its end and the renderer
// queues them again while drawing: use the count sampled after rendering.
static int sStreamRequests;

static bool
StreamingQuiet(void)
{
	return sStreamRequests == 0 && CStreaming::ms_numPriorityRequests == 0 &&
		CStreaming::ms_channel[0].state == CHANNELSTATE_IDLE &&
		CStreaming::ms_channel[1].state == CHANNELSTATE_IDLE && !CRenderer::m_loadingPriority;
}

static float
CardSeconds(void)
{
	const BenchEntry &e = sEntries[sCur];
	if(e.scene >= 0)
		return 1.5f;
	const BenchVariant &v = gBenchVariants[e.var];
	if(v.cap && !(BenchPlatform::Caps() & v.cap))
		return 0.0f;
	return sIsoVp == e.vp ? 0.5f : 1.5f;
}

static void
FinishBoot(void)
{
	CPlayerPed *p = FindPlayerPed();
	sScriptsFrozen = true;
	sMissionsBlocked = true;
	if(p->InVehicle())
		TakePlayerOutOfCar(p);
	CWorld::Players[CWorld::PlayerInFocus].MakePlayerSafe(true);
	CWorld::SetAllCarsCanBeDamaged(true);
	p->SetWantedLevel(0);
	CWanted::SetMaximumWantedLevel(0);
	CMessages::ClearMessages();
	CHud::GetRidOfAllHudMessages();
	TheCamera.SetWideScreenOff();
	ForceFadeIn();
	HidePlayer();
	SaveRender();
	sHour = CClock::GetHours();
	sWeather = CWeather::NewWeatherType;
	// Boot has rendered enough frames to validate the Vita GPU/CPU probes.
	// The earlier settings snapshot may have been taken before GL existed.
	sRun.caps = BenchPlatform::Caps();
	BenchPlatform::DeviceInfo(&sRun.device);
	sRun.bootReadyMs = NowUs() / 1000.0;
	sCur = 0;
	sLoop = 0;
	if(sNumEntries == 0)
		StartWrite();
	else
		SetPhase(PH_CARD);
}

static void
BootStep(void)
{
	CPlayerPed *p = FindPlayerPed();
	bool busy = CCutsceneMgr::ms_cutsceneLoadStatus != CUTSCENE_NOT_LOADED ||
		CCutsceneMgr::IsRunning() || CCutsceneMgr::IsCutsceneProcessing();
	// the intro cutscene comes after the init missions: story missions stay blocked from here
	if(busy)
		sMissionsBlocked = true;
	if(CCutsceneMgr::IsRunning() && !CCutsceneMgr::WasCutsceneSkipped()){
		CCutsceneMgr::mCutsceneSkipFading = true;
		CCutsceneMgr::mCutsceneSkipFadeTime = 0;
	}
	CHud::m_Wants_To_Draw_Hud = false;
	if(sPhaseTime >= 25.0f){
		if(p == nil){
			sRun.aborted = true;
			StartWrite();
			return;
		}
		if(busy){
			if(CCutsceneMgr::IsRunning())
				CCutsceneMgr::FinishCutscene();
			CCutsceneMgr::DeleteCutsceneData();
		}
		FinishBoot();
	}else if(p && !busy && sPhaseTime >= (sMissionsBlocked ? 3.0f : 10.0f))
		FinishBoot();	// without an intro cutscene, give main.scm time to set the world up
}

static void
SettleStep(void)
{
	sBenchTime += sFrameStep;
	sQuiet = StreamingQuiet() ? sQuiet + 1 : 0;
	bool done;
	if(sSettleFrames > 0)
		done = sPhaseFrames >= sSettleFrames;
	else
		done = sBenchTime >= sSettleMax || (sBenchTime >= sSettleMin && sQuiet >= sQuietNeed);
	if(sPhaseTime > 30.0f)
		done = true;
	if(!done)
		return;
	if(sEntries[sCur].scene < 0 && sIsoStage == 0){
		// viewpoint populated and streamed: freeze it for the render-only variants
		if(!IsRunningVariant(gBenchVariants[sEntries[sCur].var])){
			// finish what is still in flight so the frozen scene is complete
			CTimer::Suspend();
			CStreaming::LoadAllRequestedModels(false);
			CTimer::Resume();
			CTimer::SetCodePause(true);
			sIsoFrozen = true;
		}
		IsoApplyVariant();
		return;
	}
	sPump = false;
	StartMeasure();
}

static void
MeasureStep(void)
{
	const BenchEntry &e = sEntries[sCur];
	sMeasFrames++;
	sBenchTime += sFrameStep;
	bool done = false;
#ifndef PSP2
	if(sTest && sMeasFrames == 3)
		RequestShot(sTest->id, "a");
	else if(sTest && e.scene >= 0 && sTargetFrames > 6 && sMeasFrames == sTargetFrames / 2)
		RequestShot(sTest->id, "b");
#endif
	// StopMeasuring drops the frame in progress: finish one frame later so
	// exactly sTargetFrames are recorded
	if(sTargetFrames > 0)
		done = sMeasFrames > sTargetFrames;
	if(e.scene >= 0){
		const BenchScene &s = gBenchScenes[e.scene];
		if(s.setup == BSET_MAYHEM)
			MayhemStep();
		if(s.setup == BSET_TELEPORT){
			uint64_t now = NowUs();
			if(StreamingQuiet()){
				if(sQuiet++ == 0)
					sQuietStartUs = now;
			}else
				sQuiet = 0;
			if(sQuiet >= QUIET_FRAMES){
				sSettleMs = (sQuietStartUs - sMeasStartUs) / 1000.0;
				done = true;
			}else if(now - sMeasStartUs >= (uint64_t)(s.seconds * 1000000.0f)){
				sSettleMs = (now - sMeasStartUs) / 1000.0;
				done = true;
			}
		}else if(sTargetFrames == 0){
			if(s.seconds > 0.0f)
				done = sBenchTime >= s.seconds;
			else
				done = sCamMode != CAM_PATH || sPathDist >= sPathLen;
		}
	}
	if(!done && sPhaseTime > 300.0f){
		sInterrupted = true;
		done = true;
	}
	if(done)
		FinishTest();
}

static int
NumViewpointPages(const char **names, int max)
{
	int n = 0;
	for(int i = 0; i < sRun.numDeltas && i < BENCH_MAX_DELTAS && n < max; i++){
		bool seen = false;
		for(int j = 0; j < n; j++)
			if(strcmp(names[j], sRun.deltas[i].viewpoint) == 0)
				seen = true;
		if(!seen)
			names[n++] = sRun.deltas[i].viewpoint;
	}
	return n;
}

static int
NumPages(void)
{
	const char *names[4];
	return 2 + NumViewpointPages(names, 4);
}

static void
ResultsStep(void)
{
	int pages = NumPages();
#ifndef PSP2
	// scene validation: capture every results page, one per second
	if(sCfg.shots){
		static int shotPage = -1;
		int want = (int)sPhaseTime;
		if(want < pages && want != shotPage && sPhaseTime - want > 0.5f){
			char tag[8];
			sPage = want;
			sprintf(tag, "%d", want);
			RequestShot("results", tag);
			shotPage = want;
		}
	}
#endif
	if(Pressed(BENCH_BTN_UP))
		sPage = (sPage + pages - 1) % pages;
	if(Pressed(BENCH_BTN_DOWN) || Pressed(BENCH_BTN_TRIANGLE))
		sPage = (sPage + 1) % pages;
	bool quit = sPhaseTime > 1.0f && (Pressed(BENCH_BTN_CROSS) || Pressed(BENCH_BTN_START));
	if(sCfg.autoExit && sPhaseTime >= 30.0f)
		quit = true;
	if(quit){
		BenchPlatform::FlushLog();
		sQuit = true;
		SetPhase(PH_QUIT);
	}
}

// ---- 2D -------------------------------------------------------------------------------

static const CRGBA kBg(10, 12, 20, 255);
static const CRGBA kAccent(255, 150, 40, 255);
static const CRGBA kWhite(255, 255, 255, 255);
static const CRGBA kGrey(175, 180, 195, 255);
static const CRGBA kDim(115, 120, 135, 255);
static const CRGBA kGood(120, 220, 120, 255);
static const CRGBA kBad(240, 95, 80, 255);

#define VX(x) SCREEN_SCALE_X(x)
#define VY(y) SCREEN_SCALE_Y(y)

// UTF-8 -> plain ASCII for the game font (accents dropped)
static void
ToAsciiText(const char *s, char *out, int size)
{
	static const char latin1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
	int n = 0;
	while(*s && n < size - 1){
		unsigned char c = (unsigned char)*s;
		if(c < 0x80){
			out[n++] = (char)c;
			s++;
			continue;
		}
		int len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
		unsigned char c1 = (unsigned char)s[1];
		unsigned char c2 = c1 ? (unsigned char)s[2] : 0;
		if(c == 0xC3 && c1 >= 0x80 && c1 <= 0xBF)
			out[n++] = latin1[c1 - 0x80];
		else if(c == 0xC5 && (c1 == 0x92 || c1 == 0x93))
			out[n++] = c1 == 0x92 ? 'O' : 'o';
		else if((c == 0xC2 && c1 == 0xA0) || (c == 0xE2 && c1 == 0x80 && (c2 == 0xAF || c2 == 0x89)))
			out[n++] = ' ';
		else if(c == 0xE2 && c1 == 0x80 && (c2 == 0x98 || c2 == 0x99))
			out[n++] = '\'';
		else if(c == 0xE2 && c1 == 0x80 && (c2 == 0x93 || c2 == 0x94))
			out[n++] = '-';
		else
			out[n++] = '?';
		while(len-- > 0 && *s)
			s++;
	}
	out[n] = '\0';
}

static void
Print(float x, float y, float sx, float sy, const CRGBA &col, const char *fmt, ...)
{
	char buf[256], ascii[256];
	wchar u[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	ToAsciiText(buf, ascii, sizeof(ascii));
	AsciiToUnicode(ascii, u);
	CFont::SetBackgroundOff();
	CFont::SetBackGroundOnlyTextOff();
	CFont::SetPropOn();
	CFont::SetFontStyle(FONT_STANDARD);
	CFont::SetScale(VX(sx), VY(sy));
	CFont::SetJustifyOff();
	CFont::SetRightJustifyOff();
	CFont::SetDropShadowPosition(0);
	CFont::SetAlphaFade(255.0f);
	CFont::SetColor(col);
	CFont::SetCentreOff();
	CFont::SetWrapx(SCREEN_WIDTH - VX(20.0f));
	CFont::PrintString(x, y, u);
}

static void
Background(void)
{
	CFont::DrawFonts();	// flush text queued by the HUD so the panel covers it
	CSprite2d::DrawRect(CRect(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT), kBg);
	CSprite2d::DrawRect(CRect(0.0f, VY(56.0f), SCREEN_WIDTH, VY(58.0f)), kAccent);
	Print(VX(40.0f), VY(22.0f), 0.7f, 1.1f, kAccent, "reLCS BENCHMARK");
}

static void
DrawCard(const char *header, const char *title, const char *desc, float progress)
{
	Background();
	Print(VX(40.0f), VY(100.0f), 0.45f, 0.75f, kGrey, "%s", header);
	Print(VX(40.0f), VY(145.0f), 0.9f, 1.5f, kWhite, "%s", title);
	Print(VX(40.0f), VY(200.0f), 0.45f, 0.75f, kGrey, "%s", desc);
	float l = VX(40.0f), r = SCREEN_WIDTH - VX(40.0f);
	CSprite2d::DrawRect(CRect(l, VY(380.0f), r, VY(388.0f)), CRGBA(40, 44, 60, 255));
	CSprite2d::DrawRect(CRect(l, VY(380.0f), l + (r - l) * Clamp(progress, 0.0f, 1.0f), VY(388.0f)), kAccent);
#ifdef PSP2
	Print(VX(40.0f), VY(400.0f), 0.4f, 0.6f, kDim, "START + SELECT (1 s) : interrompre");
#else
	Print(VX(40.0f), VY(400.0f), 0.4f, 0.6f, kDim, "Echap + Tab (1 s) : interrompre");
#endif
}

static void
DrawTestCard(void)
{
	const BenchEntry &e = sEntries[sCur];
	char name[64], header[128];
	EntryName(e, name, sizeof(name));
	int n = sCur + 1 + sLoop * sNumEntries;
	sprintf(header, "%s  -  Test %d/%d", BenchGroupLabel(EntryGroup(e)), n, ProgressTotal());
	if(sCfg.loops > 1)
		sprintf(header + strlen(header), "  (boucle %d/%d)", sLoop + 1, sCfg.loops);
	const char *desc = e.scene >= 0 ? gBenchScenes[e.scene].desc : gBenchVariants[e.var].desc;
	DrawCard(header, name, desc, (float)ProgressDone() / ProgressTotal());
	if(sPhase == PH_SETTLE)
		Print(VX(40.0f), VY(250.0f), 0.4f, 0.6f, kDim, "Preparation de la scene...");
}

static void
DrawOverlay(void)
{
	char name[64];
	EntryName(sEntries[sCur], name, sizeof(name));
	Print(VX(8.0f), VY(6.0f), 0.35f, 0.55f, kWhite, "%d/%d  %s", ProgressDone() + 1, ProgressTotal(), name);
}

static const char *
VariantName(const char *id)
{
	for(int i = 0; i < gBenchNumVariants; i++)
		if(strcmp(gBenchVariants[i].id, id) == 0)
			return gBenchVariants[i].name;
	return id;
}

static void
ScoreText(char *buf, double v)
{
	if(sRun.scores.valid && v > 0.0)
		sprintf(buf, "%.0f", v);
	else
		strcpy(buf, "--");
}

static void
DrawScoresPage(void)
{
	char g[16], c[16], o[16];
	ScoreText(g, sRun.scores.graphics);
	ScoreText(c, sRun.scores.cpu);
	ScoreText(o, sRun.scores.overall);
	Print(VX(40.0f), VY(70.0f), 0.55f, 0.9f, kWhite, "Score global: %s", o);
	Print(VX(250.0f), VY(74.0f), 0.45f, 0.75f, kGrey, "Graphismes: %s    CPU: %s", g, c);
	Print(VX(40.0f), VY(100.0f), 0.38f, 0.6f, kDim, "Test");
	Print(VX(300.0f), VY(100.0f), 0.38f, 0.6f, kDim, "IPS");
	Print(VX(360.0f), VY(100.0f), 0.38f, 0.6f, kDim, "1%% bas");
	Print(VX(430.0f), VY(100.0f), 0.38f, 0.6f, kDim, "ms moy.");
	float y = 114.0f;
	int rows = 0;
	for(int i = 0; sRun.tests && i < sRun.numTests && rows < 21; i++){
		const BenchTestResult &t = sRun.tests[i];
		if(strcmp(t.group, "isolation") == 0)
			continue;
		const CRGBA &col = t.skipped || t.interrupted ? kDim : kWhite;
		Print(VX(40.0f), VY(y), 0.38f, 0.6f, col, "%.40s", t.name);
		if(t.skipped)
			Print(VX(300.0f), VY(y), 0.38f, 0.6f, kDim, "ignore: %s", t.skipReason);
		else if(t.kind == BTK_TELEPORT)
			Print(VX(300.0f), VY(y), 0.38f, 0.6f, kGrey, "chargement %.0f ms, streaming %.0f ms", t.loadMs, t.settleMs);
		else{
			Print(VX(300.0f), VY(y), 0.38f, 0.6f, col, "%.1f", t.fps);
			Print(VX(360.0f), VY(y), 0.38f, 0.6f, col, "%.1f", t.frameMs.low1Fps);
			Print(VX(430.0f), VY(y), 0.38f, 0.6f, col, "%.2f%s", t.frameMs.mean, t.interrupted ? "  (interrompu)" : "");
		}
		y += 13.0f;
		rows++;
	}
	if(rows == 0)
		Print(VX(40.0f), VY(y), 0.4f, 0.65f, kGrey, "Aucun test mesure.");
}

static void
DrawTargetsPage(void)
{
	Print(VX(40.0f), VY(70.0f), 0.55f, 0.9f, kWhite, "Quoi optimiser en priorite");
	float y = 100.0f;
	for(int i = 0; i < sRun.numTargets && i < 10; i++){
		const BenchTarget &t = sRun.targets[i];
		Print(VX(40.0f), VY(y), 0.42f, 0.66f, kWhite, "%d. %s", i + 1, t.what);
		Print(VX(330.0f), VY(y), 0.42f, 0.66f, kAccent, "%.2f ms  (%.0f%%)", t.ms, t.pct);
		Print(VX(56.0f), VY(y + 13.0f), 0.34f, 0.54f, kGrey, "%.110s", t.advice);
		y += 29.0f;
	}
	if(sRun.numTargets == 0)
		Print(VX(40.0f), VY(y), 0.4f, 0.65f, kGrey, "Pas assez de donnees pour classer les couts.");
}

static void
DrawDeltaPage(const char *viewpoint)
{
	int idx[BENCH_MAX_DELTAS], n = 0;
	for(int i = 0; i < sRun.numDeltas && i < BENCH_MAX_DELTAS; i++)
		if(strcmp(sRun.deltas[i].viewpoint, viewpoint) == 0)
			idx[n++] = i;
	for(int i = 1; i < n; i++)	// largest saving first
		for(int j = i; j > 0 && sRun.deltas[idx[j]].savedMs > sRun.deltas[idx[j - 1]].savedMs; j--){
			int t = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = t;
		}
	const char *vpName = viewpoint;
	for(int i = 0; i < gBenchNumViewpoints; i++)
		if(strcmp(gBenchViewpoints[i].id, viewpoint) == 0)
			vpName = gBenchViewpoints[i].name;
	Print(VX(40.0f), VY(70.0f), 0.55f, 0.9f, kWhite, "Isolation: %s", vpName);
	Print(VX(40.0f), VY(100.0f), 0.38f, 0.6f, kDim, "Variante (gain par image, positif = cout de l'option)");
	Print(VX(330.0f), VY(100.0f), 0.38f, 0.6f, kDim, "ms");
	Print(VX(400.0f), VY(100.0f), 0.38f, 0.6f, kDim, "%%");
	Print(VX(460.0f), VY(100.0f), 0.38f, 0.6f, kDim, "image ms");
	float y = 114.0f;
	for(int k = 0; k < n && k < 21; k++){
		const BenchDelta &d = sRun.deltas[idx[k]];
		const CRGBA &col = d.savedMs > 0.05 ? kGood : d.savedMs < -0.05 ? kBad : kGrey;
		Print(VX(40.0f), VY(y), 0.38f, 0.6f, kWhite, "%s", VariantName(d.variant));
		Print(VX(330.0f), VY(y), 0.38f, 0.6f, col, "%+.2f", d.savedMs);
		Print(VX(400.0f), VY(y), 0.38f, 0.6f, col, "%+.1f", d.savedPct);
		Print(VX(460.0f), VY(y), 0.38f, 0.6f, kGrey, "%.2f", d.variantMs);
		y += 13.0f;
	}
}

static void
DrawResults(void)
{
	Background();
	int pages = NumPages();
	sPage = Clamp(sPage, 0, pages - 1);
	Print(VX(290.0f), VY(24.0f), 0.5f, 0.85f, kWhite, "Resultats%s", sRun.aborted ? " (interrompu)" : "");
	if(!sInit)
		Print(VX(40.0f), VY(90.0f), 0.5f, 0.8f, kBad, "Erreur: memoire insuffisante pour les mesures.");
	else if(sPage == 0)
		DrawScoresPage();
	else if(sPage == 1)
		DrawTargetsPage();
	else{
		const char *names[4];
		NumViewpointPages(names, 4);
		DrawDeltaPage(names[sPage - 2]);
	}
	if(sInit && !sWriteOk)
		Print(VX(40.0f), VY(392.0f), 0.38f, 0.6f, kBad, "Erreur d'ecriture des fichiers de resultats");
	else
		Print(VX(40.0f), VY(392.0f), 0.34f, 0.54f, kDim, "Fichiers: %s", sRun.folder);
#ifdef PSP2
	Print(VX(40.0f), VY(408.0f), 0.38f, 0.6f, kGrey, "Page %d/%d   HAUT/BAS/TRIANGLE: page   CROIX/START: quitter", sPage + 1, pages);
#else
	Print(VX(40.0f), VY(408.0f), 0.38f, 0.6f, kGrey, "Page %d/%d   Haut/Bas: page   Entree/Echap: quitter", sPage + 1, pages);
#endif
	if(sCfg.autoExit && sPhase == PH_RESULTS)
		Print(VX(40.0f), VY(424.0f), 0.34f, 0.54f, kDim, "Fermeture automatique dans %d s", (int)Max(0.0f, 30.0f - sPhaseTime));
}

// ---- Public API -------------------------------------------------------------------

bool
Bench::Active(void)
{
	return sActive;
}

bool
Bench::PreInitCommandLine(const char *arg)
{
	if(arg == nil)
		return false;
	if(strcmp(arg, "-benchmark") == 0){
		sActive = true;
		return true;
	}
	if(strncmp(arg, "-benchmark=", 11) == 0){
		sActive = true;
		CopyStr(sCmdTests, arg + 11, sizeof(sCmdTests));
		return true;
	}
	return false;
}

void
Bench::OnSettingsLoaded(void)
{
	if(!sActive)
		return;
	BenchPlatform::Init();
	LoadIni();
	BuildEntries();
	for(int i = 0; i < MAX_BENCH_CARS; i++)
		sCarHandles[i] = -1;

	sInit = BenchMetrics::Init(&sRun, BENCH_MAX_FRAMES);
	if(sRun.timestamp[0] == '\0')
		BenchPlatform::Timestamp(sRun.timestamp, sizeof(sRun.timestamp));
	if(sRun.folder[0] == '\0')
		snprintf(sRun.folder, sizeof(sRun.folder), "%s%s/", BenchPlatform::ResultsRoot(), sRun.timestamp);
	if(sRun.device.platform[0] == '\0')
		BenchPlatform::DeviceInfo(&sRun.device);
	if(sRun.caps == 0)
		sRun.caps = BenchPlatform::Caps();
	sRun.bootEngineMs = NowUs() / 1000.0;
	sRun.bootGameMs = -1.0;
	sRun.bootReadyMs = -1.0;

	// Forced in memory only (AllowSettingsSave is false): comparable runs
	FrontEndMenuManager.m_PrefsVsync = 0;
	FrontEndMenuManager.m_PrefsVsyncDisp = 0;
	FrontEndMenuManager.m_PrefsFrameLimiter = 0;
	FrontEndMenuManager.m_PrefsLOD = 1.2f;
	CRenderer::ms_lodDistScale = 1.2f;
	FrontEndMenuManager.m_PrefsShowSubtitles = 0;
	CIniFile::PedNumberMultiplier = 0.6f;	// IniFile.cpp defaults
	CIniFile::CarNumberMultiplier = 0.8f;
	CPopulation::MaxNumberOfPedsInUse = DEFAULT_MAX_NUMBER_OF_PEDS * CIniFile::PedNumberMultiplier;
	CPopulation::MaxNumberOfPedsInUseInterior = DEFAULT_MAX_NUMBER_OF_PEDS_INTERIOR * CIniFile::PedNumberMultiplier;
	CCarCtrl::MaxNumberOfCarsInUse = DEFAULT_MAX_NUMBER_OF_CARS * CIniFile::CarNumberMultiplier;
	CCamera::m_bUseMouse3rdPerson = false;
#ifdef FREE_CAM
	CCamera::bFreeCam = false;
#endif
	CGame::bDemoMode = false;

	char tests[128];
	CopyStr(tests, TestList(), sizeof(tests));
	for(char *c = tests; *c; c++)
		if(*c == ';' || *c == '=') *c = ',';
	snprintf(sRun.settings, sizeof(sRun.settings),
		"mode=%s;tests=%s;loops=%d;fixedStep=%d;seed=%d;overlay=%d;autoExit=%d;selected=%d;"
		"vsync=0;frameLimiter=0;drawDistance=%.3f;pedNumberMult=%.2f;carNumberMult=%.2f;"
		"maxPeds=%d;maxCars=%d;freeCam=0;mouseCam=0;subtitles=0",
		sCfg.quick ? "quick" : "full", tests, sCfg.loops, sCfg.fixedStep, sCfg.seed,
		sCfg.overlay, sCfg.autoExit, sNumEntries, CRenderer::ms_lodDistScale,
		CIniFile::PedNumberMultiplier, CIniFile::CarNumberMultiplier,
		CPopulation::MaxNumberOfPedsInUse, CCarCtrl::MaxNumberOfCarsInUse);

	BenchPlatform::SetPeriodicReports(false);
}

bool
Bench::AllowSettingsSave(void)
{
	return !sActive;
}

bool
Bench::SkipFrontend(void)
{
	return sActive;
}

void
Bench::OnGameInitialised(void)
{
	if(!sActive)
		return;
	sRun.bootGameMs = NowUs() / 1000.0;
	CGame::bDemoMode = false;
	// GL exists now: GPU capabilities are known (unverified at OnSettingsLoaded)
	sRun.caps = BenchPlatform::Caps();
	BenchPlatform::DeviceInfo(&sRun.device);
	if(sRun.device.screenW == 0){
		sRun.device.screenW = (int)SCREEN_WIDTH;
		sRun.device.screenH = (int)SCREEN_HEIGHT;
	}
	if(sInit)
		SetPhase(PH_BOOT);
	else{
		CTimer::SetCodePause(true);
		SetPhase(PH_RESULTS);
	}
}

void
Bench::FrameBegin(void)
{
	if(sActive && sInit)
		BenchMetrics::FrameBegin(NowUs());
}

void
Bench::FrameEnd(void)
{
	if(!sActive || !sInit)
		return;
	CVector cam = TheCamera.GetPosition();
	int peds = CPools::GetPedPool() ? CPools::GetPedPool()->GetNoOfUsedSpaces() : 0;
	int vehs = CPools::GetVehiclePool() ? CPools::GetVehiclePool()->GetNoOfUsedSpaces() : 0;
	int objs = CPools::GetObjectPool() ? CPools::GetObjectPool()->GetNoOfUsedSpaces() : 0;
	sStreamRequests = CStreaming::ms_numModelsRequested;
	BenchMetrics::SetFrameContext(cam.x, cam.y, cam.z, peds, vehs, objs,
		CStreaming::ms_numModelsRequested, CTimer::GetIsPaused(), sGpuSync);
	BenchMetrics::FrameEnd(NowUs());
}

void
Bench::SwapBegin(void)
{
	if(sActive && sInit)
		BenchMetrics::SwapBegin(NowUs());
}

void
Bench::SwapEnd(void)
{
	if(sActive && sInit)
		BenchMetrics::SwapEnd(NowUs());
#ifndef PSP2
	if(sActive && sShotName[0]){
		RwGrabScreen(Scene.camera, sShotName);
		sShotName[0] = '\0';
	}
#endif
}

void
Bench::AfterTimerUpdate(void)
{
	if(!sActive)
		return;
	uint64_t now = NowUs();
	float real = sFrameUs ? (now - sFrameUs) * 1e-6f : 0.0f;
	sFrameUs = now;
	sFrameStep = sFixed ? BENCH_FIXED_DT : Min(real, 0.1f);
	if(CTimer::GetIsPaused())
		return;
	if(!sFixed){
		sNonClippedValid = false;
		return;
	}
	// exactly 1/30 s per frame, fractional milliseconds carried over
	double ms = 1000.0 / 30.0 + sMsCarry;
	uint32 whole = (uint32)ms;
	sMsCarry = ms - whole;
	if(!sNonClippedValid){
		sNonClipped = CTimer::GetTimeInMillisecondsNonClipped() -
			(CTimer::GetTimeInMilliseconds() - CTimer::GetPreviousTimeInMilliseconds());
		sNonClippedValid = true;
	}
	sNonClipped += whole;
	CTimer::SetTimeInMilliseconds(CTimer::GetPreviousTimeInMilliseconds() + whole);
	CTimer::SetTimeInMillisecondsNonClipped(sNonClipped);
	CTimer::SetTimeStep(50.0f / 30.0f);
	CTimer::SetTimeStepNonClipped(50.0f / 30.0f);
}

void
Bench::AfterPadUpdate(void)
{
	if(!sActive)
		return;
	BenchPlatform::Tick();
	sBtnPrev = sBtn;
	sBtn = BenchPlatform::RawButtons();
	const uint32_t combo = BENCH_BTN_START | BENCH_BTN_SELECT;
	if((sBtn & combo) == combo){
		uint64_t now = NowUs();
		if(sComboUs == 0)
			sComboUs = now;
		else if(now - sComboUs >= 1000000 && sPhase >= PH_BOOT && sPhase <= PH_MEASURE)
			sAbort = true;
	}else
		sComboUs = 0;

	// nothing reaches the game: pads, keyboard and mouse neutral
	for(int i = 0; i < MAX_PADS; i++){
		CPad::GetPad(i)->NewState.Clear();
		CPad::GetPad(i)->OldState.Clear();
	}
	CPad::NewKeyState.Clear();
	CPad::OldKeyState.Clear();
	CPad::NewMouseControllerState.Clear();
	CPad::OldMouseControllerState.Clear();
	CGame::bDemoMode = false;
	FrontEndMenuManager.m_bActivateSaveMenu = false;
	FrontEndMenuManager.m_bStartUpFrontEndRequested = false;
	if(FrontEndMenuManager.m_bMenuActive){
		FrontEndMenuManager.m_bMenuActive = false;
		CTimer::EndUserPause();
	}
}

bool
Bench::ScriptsEnabled(void)
{
	return !sActive || !sScriptsFrozen;
}

bool
Bench::MissionLaunchAllowed(void)
{
	// main.scm launches its init missions (player, world setup) during the
	// first slices; story missions only come after the intro cutscene
	return !sActive || !sMissionsBlocked;
}

void
Bench::AfterCameraProcess(void)
{
	if(!sActive || sPhase == PH_OFF || sPhase == PH_BOOT)
		return;
	if(sPhase > PH_MEASURE){
		HidePlayer();
		return;
	}
	if(sPhase == PH_MEASURE && sCamMode == CAM_PATH)
		sPathDist = Min(sPathDist + sPathSpeed * sFrameStep, sPathLen);
	UpdateCamera();
	HidePlayer();
	CClock::SetGameClock(sHour, 0);
	CHud::m_Wants_To_Draw_Hud = sHud;
}

void
Bench::AfterGameProcess(void)
{
	if(!sActive || sPhase == PH_OFF)
		return;
	uint64_t now = NowUs();
	float dt = sLastUs ? (now - sLastUs) * 1e-6f : 0.0f;
	sLastUs = now;
	sPhaseTime += dt;
	sPhaseFrames++;

	if(sAbort){
		sAbort = false;
		Abort();
	}
	if(sPhase >= PH_CARD && sPhase <= PH_MEASURE){
		if(CTimer::GetIsPaused()){
			if(sCamMode != CAM_NONE)
				ApplyCamera(sCamSrc, sCamDst);
			CHud::m_Wants_To_Draw_Hud = sHud;
		}
		if(sPump){
			bool code = CTimer::GetIsCodePaused();
			CTimer::SetCodePause(false);
			CStreaming::Update();
			CTimer::SetCodePause(code);
		}
		if(TheCamera.GetScreenFadeStatus() != FADE_0)
			ForceFadeIn();
	}

	switch(sPhase){
	case PH_BOOT:
		BootStep();
		break;
	case PH_CARD:
#ifndef PSP2
		if(sCur == 0 && sLoop == 0 && sPhaseFrames == 5)
			RequestShot("card", "0");
#endif
		if(sPhaseTime >= CardSeconds())
			Prepare();
		break;
	case PH_SETTLE:
		SettleStep();
		break;
	case PH_MEASURE:
		MeasureStep();
		break;
	case PH_WRITE:
		// one frame shows the message, the next one writes
		if(sPhaseFrames >= 1){
			BenchMetrics::Analyse();
			sWriteOk = BenchMetrics::WriteResults();
			BenchPlatform::FlushLog();
			sPage = 0;
			SetPhase(PH_RESULTS);
		}
		break;
	case PH_RESULTS:
		ResultsStep();
		break;
	}
}

void
Bench::Begin3D(void)
{
	if(sActive)
		BenchPlatform::Begin3D();
}

void
Bench::End3D(void)
{
	if(sActive)
		BenchPlatform::End3D();
}

void
Bench::Render2D(void)
{
	if(!sActive)
		return;
	switch(sPhase){
	case PH_BOOT:
		DrawCard("Demarrage", "Initialisation du benchmark...",
			"Chargement du monde et preparation des scenes de test.", 0.0f);
		break;
	case PH_CARD:
	case PH_SETTLE:
		DrawTestCard();
		break;
	case PH_MEASURE:
		if(sCfg.overlay)
			DrawOverlay();
		break;
	case PH_WRITE:
		DrawCard("Termine", "Ecriture des resultats...", sRun.folder, 1.0f);
		break;
	case PH_RESULTS:
	case PH_QUIT:
		DrawResults();
		break;
	}
}

bool
Bench::QuitRequested(void)
{
	return sActive && sQuit;
}

#endif
