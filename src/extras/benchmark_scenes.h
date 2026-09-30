#pragma once

// reLCS Benchmark: test catalogue (data only). The director (benchmark.cpp)
// turns it into the ordered list of tests: every scene, then every isolation
// viewpoint x variant. Ids are stable: results.json, the report tool and
// docs/BENCHMARK.md rely on them.

#ifdef RELCS_BENCHMARK

struct BenchPoint { float x, y, z; };

// How the director sets a scene up
enum BenchSetup {
	BSET_PATH,      // camera on a spline (flythroughs, s1)
	BSET_CROWD,     // fixed street camera, simulation running, density sweep
	BSET_FOLLOW,    // AI car cruising, game follow camera
	BSET_MAYHEM,    // ring of cars blown up, fires, riot
	BSET_TELEPORT,  // LoadScene timing then streaming settle
};

struct BenchScene {
	const char *id;
	const char *group;       // "graphics", "cpu", "streaming"
	const char *name;        // French, ASCII (drawn with the game font)
	const char *desc;        // one line, French, ASCII
	int kind;                // BenchTestKind
	int setup;               // BenchSetup
	int hour, weather;
	float density;           // ped and car density multiplier
	float seconds;           // measured benchmark time; 0 = until the path ends
	int frames;              // measured frames instead of seconds (0 = unused)
	float settleMin;         // minimum settle, benchmark seconds
	bool hud, realTime, groundPath, quick;
	const BenchPoint *path;  // key points; path[0] is the camera of static scenes
	int numPath;
	BenchPoint look;         // look-at of static scenes, first target of a path
	float speed;             // m/s along the path, 0 = path length / seconds
	float camHeight;         // added to the key points (above ground if groundPath)
	float lookAhead;         // look-ahead distance on the path
	float lookDrop;          // look-ahead target this much lower than the path
	float lookUntil;         // path fraction: look at `look` until here...
	float lookBlendEnd;      // ...then blend to the look-ahead target until here
};

struct BenchViewpoint {
	const char *id;          // "a_chinatown"
	const char *name;
	BenchPoint cam, look;
	bool quick;
};

enum BenchVariantAction {
	BVA_NONE,
	BVA_NO_PEDS,
	BVA_NO_VEHICLES,
	BVA_NO_OBJECTS,
	BVA_NO_WORLD_LOD,
	BVA_NO_WORLD_OPAQUE,
	BVA_NO_WORLD_TRANSPARENT,
	BVA_NO_WATER,
	BVA_NO_SKY,
	BVA_NO_SHADOWS,
	BVA_NO_CORONAS,
	BVA_NO_PARTICLES_FX,
	BVA_NO_ENVMAP,
	BVA_NO_POSTFX,
	BVA_HUD_ON,
	BVA_PS2_ALPHA_OFF,
	BVA_BACKFACE_CULL,
	BVA_LOD,                 // value = CRenderer::ms_lodDistScale
	BVA_FARCLIP,             // value = far clip
	BVA_VIEWPORT,            // value = viewport scale
	BVA_FLAT_TEXTURES,
	BVA_DRAW_TINY,
	BVA_DRAW_NULL,
	BVA_GPU_SYNC,
	BVA_NO_RENDER_3D,
	BVA_SIM_RUNNING,         // simulation unpaused
	BVA_NO_AUDIO,            // simulation unpaused, DMAudio.Service skipped
};

struct BenchVariant {
	const char *id;
	const char *name;
	const char *desc;
	int action;              // BenchVariantAction
	float value;
	unsigned int cap;        // BENCH_CAP_* needed, 0 = none
	bool quick;
};

#define BENCH_ISO_FRAMES 90

extern const BenchScene gBenchScenes[];
extern const int gBenchNumScenes;
extern const BenchViewpoint gBenchViewpoints[];
extern const int gBenchNumViewpoints;
extern const BenchVariant gBenchVariants[];
extern const int gBenchNumVariants;

// French ASCII label of a group id ("graphics" -> "Graphismes")
const char *BenchGroupLabel(const char *group);

#endif
