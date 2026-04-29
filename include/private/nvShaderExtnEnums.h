/*********************************************************************************************************\
|*                                                                                                        *|
|* SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.  *|
|* SPDX-License-Identifier: MIT                                                                           *|
|*                                                                                                        *|
|* Permission is hereby granted, free of charge, to any person obtaining a                                *|
|* copy of this software and associated documentation files (the "Software"),                             *|
|* to deal in the Software without restriction, including without limitation                              *|
|* the rights to use, copy, modify, merge, publish, distribute, sublicense,                               *|
|* and/or sell copies of the Software, and to permit persons to whom the                                  *|
|* Software is furnished to do so, subject to the following conditions:                                   *|
|*                                                                                                        *|
|* The above copyright notice and this permission notice shall be included in                             *|
|* all copies or substantial portions of the Software.                                                    *|
|*                                                                                                        *|
|* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR                             *|
|* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,                               *|
|* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL                               *|
|* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER                             *|
|* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING                                *|
|* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER                                    *|
|* DEALINGS IN THE SOFTWARE.                                                                              *|
|*                                                                                                        *|
|*                                                                                                        *|
\*********************************************************************************************************/

////////////////////////////////////////////////////////////////////////////////
////////////////////////// NVIDIA SHADER EXTENSIONS ////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// This file can be included both from HLSL shader code as well as C++ code.
// The app should call NvAPI_D3D11_IsNvShaderExtnOpCodeSupported() / NvAPI_D3D12_IsNvShaderExtnOpCodeSupported()
// to check for support for every nv shader extension opcode it plans to use



//----------------------------------------------------------------------------//
//---------------------------- NV Shader Extn Version  -----------------------//
//----------------------------------------------------------------------------//
#define NV_SHADER_EXTN_VERSION                              1

//----------------------------------------------------------------------------//
//---------------------------- Misc constants --------------------------------//
//----------------------------------------------------------------------------//
#define NV_WARP_SIZE                                       32
#define NV_WARP_SIZE_LOG2                                   5

//----------------------------------------------------------------------------//
//---------------------------- opCode constants ------------------------------//
//----------------------------------------------------------------------------//


#define NV_EXTN_OP_SHFL                                     1
#define NV_EXTN_OP_SHFL_UP                                  2
#define NV_EXTN_OP_SHFL_DOWN                                3
#define NV_EXTN_OP_SHFL_XOR                                 4

#define NV_EXTN_OP_VOTE_ALL                                 5
#define NV_EXTN_OP_VOTE_ANY                                 6
#define NV_EXTN_OP_VOTE_BALLOT                              7

#define NV_EXTN_OP_GET_LANE_ID                              8
#define NV_EXTN_OP_FP16_ATOMIC                             12
#define NV_EXTN_OP_FP32_ATOMIC                             13

#define NV_EXTN_OP_GET_SPECIAL                             19

#define NV_EXTN_OP_UINT64_ATOMIC                           20

#define NV_EXTN_OP_MATCH_ANY                               21 

// FOOTPRINT - For Sample and SampleBias
#define NV_EXTN_OP_FOOTPRINT                               28
#define NV_EXTN_OP_FOOTPRINT_BIAS                          29

#define NV_EXTN_OP_GET_SHADING_RATE                        30

// FOOTPRINT - For SampleLevel and SampleGrad
#define NV_EXTN_OP_FOOTPRINT_LEVEL                         31
#define NV_EXTN_OP_FOOTPRINT_GRAD                          32

// SHFL Generic
#define NV_EXTN_OP_SHFL_GENERIC                            33

#define NV_EXTN_OP_VPRS_EVAL_ATTRIB_AT_SAMPLE              51
#define NV_EXTN_OP_VPRS_EVAL_ATTRIB_SNAPPED                52

// HitObject API
#define NV_EXTN_OP_HIT_OBJECT_TRACE_RAY                    67
#define NV_EXTN_OP_HIT_OBJECT_MAKE_HIT                     68
#define NV_EXTN_OP_HIT_OBJECT_MAKE_HIT_WITH_RECORD_INDEX   69
#define NV_EXTN_OP_HIT_OBJECT_MAKE_MISS                    70
#define NV_EXTN_OP_HIT_OBJECT_REORDER_THREAD               71
#define NV_EXTN_OP_HIT_OBJECT_INVOKE                       72
#define NV_EXTN_OP_HIT_OBJECT_IS_MISS                      73
#define NV_EXTN_OP_HIT_OBJECT_GET_INSTANCE_ID              74
#define NV_EXTN_OP_HIT_OBJECT_GET_INSTANCE_INDEX           75
#define NV_EXTN_OP_HIT_OBJECT_GET_PRIMITIVE_INDEX          76
#define NV_EXTN_OP_HIT_OBJECT_GET_GEOMETRY_INDEX           77
#define NV_EXTN_OP_HIT_OBJECT_GET_HIT_KIND                 78
#define NV_EXTN_OP_HIT_OBJECT_GET_RAY_DESC                 79
#define NV_EXTN_OP_HIT_OBJECT_GET_ATTRIBUTES               80
#define NV_EXTN_OP_HIT_OBJECT_GET_SHADER_TABLE_INDEX       81
#define NV_EXTN_OP_HIT_OBJECT_LOAD_LOCAL_ROOT_TABLE_CONSTANT  82
#define NV_EXTN_OP_HIT_OBJECT_IS_HIT                       83
#define NV_EXTN_OP_HIT_OBJECT_IS_NOP                       84
#define NV_EXTN_OP_HIT_OBJECT_MAKE_NOP                     85

// Micro-map API
#define NV_EXTN_OP_RT_TRIANGLE_OBJECT_POSITIONS            86
#define NV_EXTN_OP_RT_MICRO_TRIANGLE_OBJECT_POSITIONS      87
#define NV_EXTN_OP_RT_MICRO_TRIANGLE_BARYCENTRICS          88
#define NV_EXTN_OP_RT_IS_MICRO_TRIANGLE_HIT                89
#define NV_EXTN_OP_RT_IS_BACK_FACING                       90
#define NV_EXTN_OP_RT_MICRO_VERTEX_OBJECT_POSITION         91
#define NV_EXTN_OP_RT_MICRO_VERTEX_BARYCENTRICS            92

// Megageometry API
#define NV_EXTN_OP_RT_GET_CLUSTER_ID                        93
#define NV_EXTN_OP_RT_GET_CANDIDATE_CLUSTER_ID              94
#define NV_EXTN_OP_RT_GET_COMMITTED_CLUSTER_ID              95
#define NV_EXTN_OP_HIT_OBJECT_GET_CLUSTER_ID                96
#define NV_EXTN_OP_RT_CANDIDATE_TRIANGLE_OBJECT_POSITIONS   97
#define NV_EXTN_OP_RT_COMMITTED_TRIANGLE_OBJECT_POSITIONS   98
#define NV_EXTN_OP_HIT_OBJECT_GET_TRIANGLE_OBJECT_POSITIONS 99

// Linear Swept Sphere API
#define NV_EXTN_OP_RT_SPHERE_OBJECT_POSITION_AND_RADIUS             100
#define NV_EXTN_OP_RT_CANDIDATE_SPHERE_OBJECT_POSITION_AND_RADIUS   101
#define NV_EXTN_OP_RT_COMMITTED_SPHERE_OBJECT_POSITION_AND_RADIUS   102
#define NV_EXTN_OP_HIT_OBJECT_GET_SPHERE_OBJECT_POSITION_AND_RADIUS 103
#define NV_EXTN_OP_RT_LSS_OBJECT_POSITIONS_AND_RADII                104
#define NV_EXTN_OP_RT_CANDIDATE_LSS_OBJECT_POSITIONS_AND_RADII      105
#define NV_EXTN_OP_RT_COMMITTED_LSS_OBJECT_POSITIONS_AND_RADII      106
#define NV_EXTN_OP_HIT_OBJECT_GET_LSS_OBJECT_POSITIONS_AND_RADII    107
#define NV_EXTN_OP_RT_IS_SPHERE_HIT                                 108
#define NV_EXTN_OP_RT_CANDIDATE_IS_NONOPAQUE_SPHERE                 109
#define NV_EXTN_OP_RT_COMMITTED_IS_SPHERE                           110
#define NV_EXTN_OP_HIT_OBJECT_IS_SPHERE_HIT                         111
#define NV_EXTN_OP_RT_IS_LSS_HIT                                    112
#define NV_EXTN_OP_RT_CANDIDATE_IS_NONOPAQUE_LSS                    113
#define NV_EXTN_OP_RT_COMMITTED_IS_LSS                              114
#define NV_EXTN_OP_HIT_OBJECT_IS_LSS_HIT                            115
#define NV_EXTN_OP_RT_CANDIDATE_LSS_HIT_PARAMETER                   116
#define NV_EXTN_OP_RT_COMMITTED_LSS_HIT_PARAMETER                   117
#define NV_EXTN_OP_RT_CANDIDATE_BUILTIN_PRIMITIVE_RAY_T             118
#define NV_EXTN_OP_RT_COMMIT_NONOPAQUE_BUILTIN_PRIMITIVE_HIT        119
//----------------------------------------------------------------------------//
//-------------------- GET_SPECIAL subOpCode constants -----------------------//
//----------------------------------------------------------------------------//
#define NV_SPECIALOP_THREADLTMASK                           4
#define NV_SPECIALOP_FOOTPRINT_SINGLELOD_PRED               5
#define NV_SPECIALOP_GLOBAL_TIMER_LO                        9
#define NV_SPECIALOP_GLOBAL_TIMER_HI                       10

//----------------------------------------------------------------------------//
//----------------------------- Texture Types  -------------------------------//
//----------------------------------------------------------------------------//
#define NV_EXTN_TEXTURE_1D                                  2
#define NV_EXTN_TEXTURE_1D_ARRAY                            3
#define NV_EXTN_TEXTURE_2D                                  4
#define NV_EXTN_TEXTURE_2D_ARRAY                            5
#define NV_EXTN_TEXTURE_3D                                  6
#define NV_EXTN_TEXTURE_CUBE                                7
#define NV_EXTN_TEXTURE_CUBE_ARRAY                          8


//---------------------------------------------------------------------------//
//----------------FOOTPRINT Enums for NvFootprint* extns---------------------//
//---------------------------------------------------------------------------//
#define NV_EXTN_FOOTPRINT_MODE_FINE                         0
#define NV_EXTN_FOOTPRINT_MODE_COARSE                       1

//----------------------------------------------------------------------------//
//--------------------------- Cluster Constants  -----------------------------//
//----------------------------------------------------------------------------//

#define NV_EXTN_CLUSTER_ID_INVALID                 0xffffffff

