// kernel.cu
#include "generation/gpu_terrain/kernel.cuh"
#include "generation/device_shared/noise.hpp"
#include "generation/device_shared/spline.hpp"
#include "generation/device_shared/biome_rules.hpp"
#include "world/chunk.hpp"

#include <stdexcept>

__constant__ TerrainParams gTerrainParams;

void uploadTerrainParams(const TerrainParams& p) {
    cudaError_t err = cudaMemcpyToSymbol(
        gTerrainParams,
        &p,
        sizeof(TerrainParams),
        0,
        cudaMemcpyHostToDevice
    );
    if (err != cudaSuccess)
        throw std::runtime_error(std::string("uploadTerrainParams failed: ") + cudaGetErrorString(err));
}

__global__
void terrainKernel(BlockType* blocks, int originX, int originY, int originZ)
{
    const TerrainParams& P = gTerrainParams;   // alias for readability

    // =====================================================================
    // STEP 1 — DENSITY
    // =====================================================================
    int lx = blockIdx.x * blockDim.x + threadIdx.x;
    int lz = blockIdx.y * blockDim.y + threadIdx.y;
    int ly = blockIdx.z * blockDim.z + threadIdx.z;
    if (lx >= CHUNK_SIZE || lz >= CHUNK_SIZE || ly >= CHUNK_SIZE) return;

    int idx = lx + CHUNK_SIZE * (lz + CHUNK_SIZE * ly);

    float wx = (float)(originX + lx);
    float wy = (float)(originY + ly);
    float wz = (float)(originZ + lz);

    // Continent
    float continent = fbm2D(wx * P.continentScale, wz * P.continentScale,
                            12, P.heightLacunarity, P.heightGain);
    float c = continent * 2.0f - 1.0f;
    c = c * (1.0f + fabsf(c) * 0.8f);
    c = fmaxf(-1.0f, fminf(1.0f, c));
    float spline = heightSpline(c * 0.5f + 0.5f);

    // Mountain
    float mRaw     = fbm2D(wx * P.mountainScale, wz * P.mountainScale, 3, 2.0f, 0.5f);
    float mountain = fmaxf(0.0f, mRaw - 0.4f) * (1.0f / 0.6f);
    mountain = mountain * mountain;

    // Surface height
    float surfaceY = (float)P.baseHeight
                   + spline   * P.heightScale
                   + mountain * P.mountainAmp;

    // Detail
    float detailFreq = P.heightBaseFreq * 4.0f;
    float detail = fbm2D(wx * detailFreq + 200.0f, wz * detailFreq + 200.0f,
                         4, 2.0f, 0.55f);
    detail = (detail - 0.5f) * 18.0f;
    surfaceY += detail;

    // Rivers
    float riverRaw      = warpedNoise2D(wx * P.riverScale, wz * P.riverScale, 8.0f);
    float riverDist     = fabsf(riverRaw - 0.5f) * 2.0f;
    float riverStrength = fmaxf(0.0f, 1.0f - riverDist / P.riverWidth);
    riverStrength       = riverStrength * riverStrength;
    float riverFloor    = (float)P.waterLevel - 2.0f;
    surfaceY = surfaceY + riverStrength * (riverFloor - surfaceY);

    // 3D density perturbation
    float pFreq   = P.heightBaseFreq * 3.0f;
    float perturb = fbm3D(wx * pFreq, wy * pFreq, wz * pFreq, 3, 1.8f, 0.5f);
    perturb = (perturb - 0.5f) * 12.0f;

    float density = (surfaceY + perturb) - wy;

    // =====================================================================
    // STEP 2 — BIOME
    // =====================================================================
    float temperature = fbm2D(wx * P.biomeScale + 1000.0f,
                              wz * P.biomeScale + 1000.0f, 3, 2.0f, 0.5f);
    float humidity    = fbm2D(wx * P.biomeScale + 5000.0f,
                              wz * P.biomeScale + 5000.0f, 3, 2.0f, 0.5f);
    temperature = fmaxf(0.0f, fminf(1.0f, temperature));
    humidity    = fmaxf(0.0f, fminf(1.0f, humidity));

    Biome biome = classifyBiome(temperature, humidity);

    if (density <= 0.0f) {
        blocks[idx] = (wy <= (float)P.waterLevel) ? BlockType::Water : BlockType::Air;
        return;
    }

    int  depth       = (int)floorf(density);
    bool isUnderwater = (wy <= (float)P.waterLevel);
    blocks[idx] = biomeBlock(biome, depth, (depth == 0), isUnderwater, (int)wy, P.waterLevel);

    // =====================================================================
    // STEP 3 — CAVE CARVE
    // =====================================================================
    if (blocks[idx] == BlockType::Air || blocks[idx] == BlockType::Water) return;

    int wyI = originY + ly;
    if (wyI <= 1) return;
    if (wyI >= P.waterLevel - 3 && wyI <= P.waterLevel + 3) return;

    float cf = P.caveBaseFreq;

    // Worley chambers
    float chamber        = worleyNoise3D(wx * cf, (float)wyI * cf, wz * cf);
    float depthBelow     = (float)P.baseHeight - (float)wyI;
    float chamberThresh  = (depthBelow > 30.0f) ? 0.12f : 0.0f;
    bool  isChambered    = (chamber < chamberThresh);

    // Worm tunnels
    float wormFreq = P.caveBaseFreq * 0.2f;
    float n1 = warpedFbm3D(wx * wormFreq,          (float)wyI * wormFreq, wz * wormFreq,
                            P.caveOctaves, P.heightLacunarity, P.heightGain, 6.0f);
    float n2 = warpedFbm3D(wx * wormFreq + 31.7f,  (float)wyI * wormFreq + 17.3f,
                            wz * wormFreq + 53.1f,
                            P.caveOctaves, P.heightLacunarity, P.heightGain, 6.0f);

    float wormR = P.caveWormRadius;
    if (isChambered || (fabsf(n1 - 0.5f) < wormR && fabsf(n2 - 0.5f) < wormR))
        blocks[idx] = BlockType::Air;
}