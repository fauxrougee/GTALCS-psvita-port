#ifdef RELCS_BENCHMARK

#include "common.h"
#include "Weather.h"
#include "benchmark_platform.h"
#include "benchmark_metrics.h"
#include "benchmark_scenes.h"

#define BENCH_N(a) (int)ARRAY_SIZE(a)

// Portland El track (DATA/PATHS/TRACKS.DAT points 58-112, ~845 m):
// Portland View -> Red Light -> Hepburn Heights
static const BenchPoint kElTrack[] = {
	{ 1296.4f, -533.4f, 42.2f }, { 1280.4f, -517.4f, 42.2f }, { 1275.9f, -513.2f, 42.0f }, { 1269.8f, -509.0f, 41.9f },
	{ 1262.0f, -505.5f, 41.9f }, { 1254.2f, -503.4f, 41.9f }, { 1245.0f, -502.6f, 42.2f }, { 1220.5f, -504.0f, 39.7f },
	{ 1195.8f, -505.4f, 37.3f }, { 1150.4f, -505.4f, 32.4f }, { 1105.0f, -505.2f, 26.9f }, { 1059.5f, -505.1f, 21.7f },
	{ 1047.6f, -504.0f, 21.7f }, { 1037.5f, -501.2f, 21.7f }, { 1027.5f, -496.6f, 21.7f }, { 1019.5f, -491.3f, 21.7f },
	{ 1013.6f, -485.8f, 21.7f }, { 1005.8f, -476.7f, 21.7f }, { 1000.6f, -467.6f, 21.7f }, { 996.9f, -457.2f, 21.9f },
	{ 995.0f, -447.7f, 21.9f }, { 994.7f, -439.7f, 21.9f }, { 994.8f, -417.1f, 21.7f }, { 994.7f, -394.3f, 21.9f },
	{ 994.6f, -371.7f, 21.7f }, { 994.6f, -348.9f, 21.7f }, { 994.7f, -326.3f, 21.9f }, { 994.7f, -303.6f, 21.9f },
	{ 995.7f, -291.6f, 21.7f }, { 998.5f, -281.5f, 21.7f }, { 1003.1f, -271.5f, 21.7f }, { 1008.6f, -263.5f, 21.7f },
	{ 1014.0f, -257.6f, 21.9f }, { 1030.3f, -240.9f, 21.9f }, { 1034.6f, -236.5f, 21.8f }, { 1038.7f, -230.3f, 21.8f },
	{ 1042.2f, -222.5f, 21.7f }, { 1044.3f, -214.8f, 21.8f }, { 1045.2f, -205.6f, 21.9f }, { 1045.2f, -183.0f, 21.9f },
	{ 1045.2f, -160.2f, 21.9f }, { 1045.3f, -137.7f, 21.7f }, { 1045.2f, -114.8f, 21.9f }, { 1045.3f, -92.3f, 21.7f },
	{ 1045.3f, -69.5f, 21.7f }, { 1045.1f, -63.3f, 21.8f }, { 1043.6f, -56.0f, 21.8f }, { 1040.6f, -48.1f, 21.8f },
	{ 1036.7f, -41.0f, 21.7f }, { 1030.8f, -34.0f, 21.9f }, { 1014.9f, -18.0f, 21.7f }, { 998.7f, -1.9f, 21.9f },
	{ 994.3f, 2.4f, 21.7f }, { 988.0f, 6.6f, 21.7f }, { 980.3f, 10.1f, 21.7f },
};

static const BenchPoint kStauntonTowers[] = {
	{ 147.0f, -1700.0f, 60.0f }, { 147.0f, -1550.0f, 60.0f }, { 147.0f, -1350.0f, 60.0f },
	{ 147.0f, -1250.0f, 80.0f }, { 147.0f, -1150.0f, 100.0f }, { 350.0f, -900.0f, 80.0f },
};

static const BenchPoint kShoresideHills[] = {
	{ -736.0f, -627.0f, 60.0f }, { -736.0f, -424.0f, 60.0f }, { -850.0f, -290.0f, 75.0f },
	{ -975.0f, -174.0f, 75.0f }, { -1020.0f, 22.0f, 90.0f }, { -1033.0f, 99.0f, 95.0f },
};

// Street level: z is the road node height, replaced by the collision ground at runtime
static const BenchPoint kPortlandStreets[] = {
	{ 1091.0f, -129.0f, 8.8f }, { 977.0f, -170.0f, 3.8f }, { 990.0f, -263.0f, 3.7f },
	{ 978.0f, -339.0f, 8.8f }, { 911.0f, -399.0f, 13.8f }, { 866.0f, -454.0f, 13.8f },
	{ 836.0f, -534.0f, 13.8f }, { 846.0f, -624.0f, 13.8f }, { 901.0f, -779.0f, 13.8f },
};

// Above the Chinatown roofs, then 6-8 m over the bridge deck (z 38-45)
static const BenchPoint kBridgeRun[] = {
	{ 900.0f, -800.0f, 55.0f }, { 810.0f, -890.0f, 55.0f }, { 734.0f, -930.0f, 52.0f },
	{ 498.0f, -930.0f, 52.0f }, { 300.0f, -1000.0f, 50.0f }, { 147.0f, -1300.0f, 50.0f },
};

static const BenchPoint kChinatownCam[] = { { 900.8f, -788.8f, 15.8f } };
static const BenchPoint kPortlandDrive[] = { { 1051.0f, -949.0f, 14.0f } };
static const BenchPoint kAtlanticQuays[] = { { 1124.4f, -1099.5f, 10.7f } };

static const BenchPoint kTpRedLight[] = { { 933.0f, -389.0f, 16.0f } };
static const BenchPoint kTpBedford[] = { { 147.0f, -1436.0f, 27.0f } };
static const BenchPoint kTpAirport[] = { { -1363.0f, -812.0f, 12.0f } };
static const BenchPoint kTpHarbor[] = { { 1600.0f, -980.0f, 20.0f } };
static const BenchPoint kTpRockford[] = { { 252.0f, -56.0f, 22.0f } };
static const BenchPoint kTpCedarGrove[] = { { -305.0f, 292.0f, 70.0f } };

#define NOLOOK { 0.0f, 0.0f, 0.0f }
#define CROWD(id, name, d, quick) \
	{ id, "cpu", name, "Chinatown a midi, pietons et voitures x" #d ": cout de la simulation et du rendu des foules.", \
	  BTK_STATIC, BSET_CROWD, 12, WEATHER_SUNNY, (float)d, 0.0f, 240, 4.0f, false, false, false, quick, \
	  kChinatownCam, 1, { 905.0f, -650.0f, 19.0f }, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
#define TELEPORT(id, name, pts, lx, ly, lz) \
	{ id, "streaming", name, "Temps de LoadScene, puis temps jusqu'a la fin du streaming (images mesurees).", \
	  BTK_TELEPORT, BSET_TELEPORT, 12, WEATHER_SUNNY, 1.0f, 10.0f, 0, 0.0f, false, false, false, false, \
	  pts, 1, { lx, ly, lz }, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }

const BenchScene gBenchScenes[] = {
	// id, group, name, desc,
	// kind, setup, hour, weather, density, seconds, frames, settleMin, hud, realTime, groundPath, quick,
	// path, numPath, look, speed, camHeight, lookAhead, lookDrop, lookUntil, lookBlendEnd
	{ "g1_portland_el", "graphics", "Portland - metro aerien",
	  "Survol du metro aerien: Portland View, Red Light et Hepburn Heights.",
	  BTK_FLYTHROUGH, BSET_PATH, 13, WEATHER_SUNNY, 1.0f, 30.0f, 0, 1.0f, false, false, false, true,
	  kElTrack, BENCH_N(kElTrack), NOLOOK, 0.0f, 3.0f, 25.0f, 1.0f, 0.0f, 0.0f },
	{ "g2_staunton_towers", "graphics", "Staunton - gratte-ciel",
	  "Vol entre les tours de Bedford Point et Torrington: geometrie lointaine et LOD.",
	  BTK_FLYTHROUGH, BSET_PATH, 13, WEATHER_SUNNY, 1.0f, 30.0f, 0, 1.0f, false, false, false, true,
	  kStauntonTowers, BENCH_N(kStauntonTowers), { 110.0f, -1364.0f, 200.0f }, 0.0f, 0.0f, 40.0f, 8.0f, 0.30f, 0.45f },
	{ "g3_shoreside_hills", "graphics", "Shoreside - collines",
	  "Survol de l'aeroport et des collines de Pike Creek en fin d'apres-midi.",
	  BTK_FLYTHROUGH, BSET_PATH, 17, WEATHER_EXTRA_SUNNY, 1.0f, 30.0f, 0, 1.0f, false, false, false, false,
	  kShoresideHills, BENCH_N(kShoresideHills), NOLOOK, 0.0f, 0.0f, 40.0f, 8.0f, 0.0f, 0.0f },
	{ "g4_portland_night_rain", "graphics", "Portland - nuit et pluie",
	  "Camera au niveau de la rue, de nuit sous la pluie: lumieres, halos et reflets.",
	  BTK_FLYTHROUGH, BSET_PATH, 23, WEATHER_RAINY, 1.0f, 30.0f, 0, 1.0f, false, false, true, false,
	  kPortlandStreets, BENCH_N(kPortlandStreets), NOLOOK, 10.0f, 3.0f, 25.0f, 1.5f, 0.0f, 0.0f },

	CROWD("c1_crowd_d0", "Foule x0", 0, false),
	CROWD("c1_crowd_d1", "Foule x1", 1, true),
	CROWD("c1_crowd_d2", "Foule x2", 2, false),
	CROWD("c1_crowd_d3", "Foule x3", 3, true),
	{ "c2_traffic_follow", "cpu", "Conduite IA",
	  "Une voiture pilotee par l'IA traverse Portland: camera de poursuite et HUD.",
	  BTK_FOLLOW, BSET_FOLLOW, 12, WEATHER_SUNNY, 1.0f, 30.0f, 0, 2.0f, true, false, false, true,
	  kPortlandDrive, 1, NOLOOK, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
	{ "c3_mayhem", "cpu", "Chaos: explosions et emeutes",
	  "8 voitures explosent une a une, incendies, particules et emeutes (densite x2).",
	  BTK_STRESS, BSET_MAYHEM, 12, WEATHER_SUNNY, 2.0f, 25.0f, 0, 2.0f, false, false, false, false,
	  kAtlanticQuays, 1, NOLOOK, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },

	{ "s1_bridge_run", "streaming", "Streaming: pont Callahan",
	  "Camera rapide de Chinatown a Staunton par le pont: changement d'ile en temps reel.",
	  BTK_STREAMING, BSET_PATH, 13, WEATHER_SUNNY, 1.0f, 0.0f, 0, 1.0f, false, true, false, true,
	  kBridgeRun, BENCH_N(kBridgeRun), NOLOOK, 40.0f, 0.0f, 40.0f, 3.0f, 0.0f, 0.0f },
	TELEPORT("s2_tp_redlight", "Teleportation: Red Light", kTpRedLight, 891.0f, -424.0f, 22.0f),
	TELEPORT("s2_tp_bedford", "Teleportation: Bedford Point", kTpBedford, 147.0f, -1100.0f, 80.0f),
	TELEPORT("s2_tp_airport", "Teleportation: aeroport", kTpAirport, -951.0f, -594.0f, 15.0f),
	TELEPORT("s2_tp_harbor", "Teleportation: port de Portland", kTpHarbor, 1491.0f, -925.0f, 10.0f),
	TELEPORT("s2_tp_rockford", "Teleportation: Rockford", kTpRockford, 300.0f, -300.0f, 30.0f),
	TELEPORT("s2_tp_cedargrove", "Teleportation: Cedar Grove", kTpCedarGrove, -600.0f, 300.0f, 60.0f),
};
const int gBenchNumScenes = BENCH_N(gBenchScenes);

const BenchViewpoint gBenchViewpoints[] = {
	{ "a_chinatown", "Chinatown", { 900.8f, -788.8f, 15.8f }, { 905.0f, -650.0f, 19.0f }, true },
	{ "b_bedford", "Bedford Point", { 146.9f, -1436.4f, 27.0f }, { 147.0f, -1100.0f, 80.0f }, false },
};
const int gBenchNumViewpoints = BENCH_N(gBenchViewpoints);

#define FROZEN "Image figee: seule cette option change par rapport a la reference."
const BenchVariant gBenchVariants[] = {
	{ "baseline", "Reference", "Image figee de reference (simulation en pause).", BVA_NONE, 0.0f, 0, true },
	{ "no_peds", "Sans pietons", FROZEN, BVA_NO_PEDS, 0.0f, 0, true },
	{ "no_vehicles", "Sans vehicules", FROZEN, BVA_NO_VEHICLES, 0.0f, 0, true },
	{ "no_objects", "Sans objets", FROZEN, BVA_NO_OBJECTS, 0.0f, 0, false },
	{ "no_world_lod", "Sans routes ni batiments LOD", FROZEN, BVA_NO_WORLD_LOD, 0.0f, 0, false },
	{ "no_world_opaque", "Sans batiments opaques", FROZEN, BVA_NO_WORLD_OPAQUE, 0.0f, 0, true },
	{ "no_world_transparent", "Sans batiments transparents", FROZEN, BVA_NO_WORLD_TRANSPARENT, 0.0f, 0, true },
	{ "no_water", "Sans eau", FROZEN, BVA_NO_WATER, 0.0f, 0, false },
	{ "no_sky", "Sans ciel", FROZEN, BVA_NO_SKY, 0.0f, 0, false },
	{ "no_shadows", "Sans ombres", FROZEN, BVA_NO_SHADOWS, 0.0f, 0, true },
	{ "no_coronas", "Sans halos", FROZEN, BVA_NO_CORONAS, 0.0f, 0, false },
	{ "no_particles_fx", "Sans particules ni effets", FROZEN, BVA_NO_PARTICLES_FX, 0.0f, 0, false },
	{ "no_envmap", "Sans reflets des vehicules", FROZEN, BVA_NO_ENVMAP, 0.0f, 0, true },
	{ "no_postfx", "Sans filtre de couleur", FROZEN, BVA_NO_POSTFX, 0.0f, 0, true },
	{ "hud_on", "Avec HUD", FROZEN, BVA_HUD_ON, 0.0f, 0, false },
	{ "ps2_alpha_off", "Sans double passe alpha PS2", FROZEN, BVA_PS2_ALPHA_OFF, 0.0f, 0, false },
	{ "backface_cull", "Elimination des faces arriere", FROZEN, BVA_BACKFACE_CULL, 0.0f, 0, false },
	{ "farclip_300", "Plan lointain a 300 m", FROZEN, BVA_FARCLIP, 300.0f, 0, false },
	{ "viewport_75", "Rendu 3D a 75%", "Surface de rendu 3D reduite: cout du remplissage.", BVA_VIEWPORT, 0.75f, BENCH_CAP_VIEWPORT, false },
	{ "viewport_50", "Rendu 3D a 50%", "Surface de rendu 3D reduite: cout du remplissage.", BVA_VIEWPORT, 0.5f, BENCH_CAP_VIEWPORT, true },
	{ "viewport_25", "Rendu 3D a 25%", "Surface de rendu 3D reduite: cout du remplissage.", BVA_VIEWPORT, 0.25f, BENCH_CAP_VIEWPORT, false },
	{ "viewport_min", "Rendu 3D sur 1 pixel", "Pixels quasi nuls: cout des sommets et du CPU seul.", BVA_VIEWPORT, 0.01f, BENCH_CAP_VIEWPORT, false },
	{ "flat_textures", "Textures unies", "Texture blanche 1x1 partout: cout des textures.", BVA_FLAT_TEXTURES, 0.0f, BENCH_CAP_FLAT_TEX, false },
	{ "draw_tiny", "Draw calls minuscules", "Chaque draw call garde son cout CPU mais presque rien pour le GPU.", BVA_DRAW_TINY, 0.0f, BENCH_CAP_DRAW_MODE, true },
	{ "draw_null", "Draw calls supprimes", "Les draw calls n'atteignent plus le pilote: cout CPU du moteur seul.", BVA_DRAW_NULL, 0.0f, BENCH_CAP_DRAW_MODE, false },
	{ "gpu_sync", "Synchro GPU", "Attente du GPU apres chaque image: temps GPU reel.", BVA_GPU_SYNC, 0.0f, BENCH_CAP_GPU_SYNC, true },
	{ "no_render_3d", "Sans rendu 3D", "Tout le rendu 3D coupe: cout fixe de l'image.", BVA_NO_RENDER_3D, 0.0f, 0, false },
	{ "baseline_end", "Reference (fin)", "Reference refaite en fin de serie: controle de derive.", BVA_NONE, 0.0f, 0, true },
	// streaming is pumped for these two: after baseline_end, the loaded models change
	{ "lod_low", "Distance d'affichage 0.925", FROZEN, BVA_LOD, 0.925f, 0, false },
	{ "lod_high", "Distance d'affichage 1.8", FROZEN, BVA_LOD, 1.8f, 0, false },
	{ "sim_running", "Simulation active", "Simulation reprise (pas fixe): cout de la simulation.", BVA_SIM_RUNNING, 0.0f, 0, true },
	{ "no_audio", "Simulation sans audio", "Simulation active, DMAudio.Service saute: cout de l'audio.", BVA_NO_AUDIO, 0.0f, 0, true },
};
const int gBenchNumVariants = BENCH_N(gBenchVariants);

const char *
BenchGroupLabel(const char *group)
{
	if(strcmp(group, "graphics") == 0) return "Graphismes";
	if(strcmp(group, "cpu") == 0) return "CPU";
	if(strcmp(group, "streaming") == 0) return "Streaming";
	if(strcmp(group, "isolation") == 0) return "Isolation";
	return group;
}

#endif
