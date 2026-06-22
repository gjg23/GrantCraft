// host_device.hpp
#pragma once

#ifdef __CUDACC__
    // Compiled by nvcc — enable CUDA qualifiers
    #define HD      __host__ __device__
    #define FINLINE __forceinline__
#else
    // Compiled by g++/clang — strip CUDA qualifiers to nothing
    #define HD
    #define FINLINE inline
#endif