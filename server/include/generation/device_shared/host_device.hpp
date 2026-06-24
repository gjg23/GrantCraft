#pragma once

#ifdef __CUDACC__

#define HD      __host__ __device__
#define HOST    __host__
#define DEV     __device__
#define FINLINE __forceinline__

#else

#define HD
#define HOST
#define DEV
#define FINLINE inline

#endif