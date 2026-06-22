// terrain_params.hpp
#pragma once
#include "world/block.hpp"
#include "settings/settings.hpp"
//#include <cuda_runtime.h>

// Biomes
enum class Biome : uint8_t {
    Desert      = 0,  // hot   + dry
    Savannah    = 1,  // hot   + mild
    Jungle      = 2,  // hot   + wet
    Rocky       = 3,  // warm  + dry
    Plains      = 4,  // warm  + mild
    Forest      = 5,  // warm  + wet
    SnowDesert  = 6,  // cold  + dry
    SnowyPlains = 7,  // cold  + mild
    SnowyForest = 8,  // cold  + wet
};

// constant GPU memory for terrain settings
struct TerrainParams {
    // Continent / macro shape
    float continentScale;   // low-freq noise scale for sea-level offsets
    float continentAmp;     // amplitude of continent height shifts

    // Terrain height FBM
    int   heightOctaves;    // number of FBM octaves
    float heightBaseFreq;   // base frequency (world units)
    float heightLacunarity; // frequency multiplier per octave
    float heightGain;       // amplitude multiplier per octave (persistence)
    float heightScale;      // overall height range (world blocks)
    int   baseHeight;       // sea level block Y

    // Mountain modifier
    float mountainScale;    // noise scale for mountain zones
    float mountainAmp;      // extra height added in mountain zones

    // River + water
    float riverScale;       // noise frequency for river centrelines
    float riverWidth;       // half-width threshold (0..1 domain)
    int   waterLevel;       // world Y of sea/river surface

    // Biome temperature + humidity
    float biomeScale;       // noise scale for temp & humidity maps

    // Cave carving
    int   caveOctaves;
    float caveBaseFreq;
    float caveThreshold;    // density value below which a voxel becomes air
    float caveWormRadius;   // secondary Worley worm radius

    // chunky chunk chunk chunk
    int chunkWidth;
    int chunkHeight;
    int worldHeightChunks;
};

// keep in gpu memory
#ifdef __CUDACC__
    extern __device__ __constant__ TerrainParams d_terrain;
#endif

// CPU-accessible mirror — always available on host
extern TerrainParams h_terrain;

// Upload to both GPU constant memory and CPU mirror
void uploadTerrainParams(const TerrainParams& p);


// Host side calls
// Call in main to set settings to device
void uploadTerrainParams(const TerrainParams& p);

// Launches the terrain kernel for one chunk
// Write into a device side blocks array
/* void launchChunkGenGPU(
    float*       d_density,
    uint8_t*     d_biome,
    BlockType*   d_blocks,
    int originX, int originY, int originZ,
    cudaStream_t stream); */

// allocate all three device buffers and return them
struct ChunkDeviceBuffers {
    float*     density  = nullptr;
    uint8_t*   biome    = nullptr;
    BlockType* blocks   = nullptr;
};

ChunkDeviceBuffers allocChunkDeviceBuffers();
void freeChunkDeviceBuffers(ChunkDeviceBuffers& b);