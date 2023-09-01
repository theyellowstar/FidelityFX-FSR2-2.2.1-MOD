// AMD Cauldron code
// 
// Copyright(c) 2020 Advanced Micro Devices, Inc.All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
#pragma once

#include "Misc/ColorConversion.h"
#include "PostProcPS.h"

namespace CAULDRON_DX12
{
    class ToneMapping
    {
    public:
        void OnCreate(Device* pDevice, ResourceViewHeaps *pResourceViewHeaps, DynamicBufferRing *pDynamicBufferRing, StaticBufferPool  *pStaticBufferPool, DXGI_FORMAT outFormat, uint32_t srvTableSize = 1, const char* shaderSource = "Tonemapping.hlsl");
        void OnDestroy();
        void UpdatePipelines(DXGI_FORMAT outFormat);
        void Draw(ID3D12GraphicsCommandList* pCommandList, CBV_SRV_UAV *pHDRSRV, float exposure, int toneMapper, bool gamma2 = false);

    protected:
        PostProcPS m_toneMapping;
        DynamicBufferRing *m_pDynamicBufferRing = NULL;

        struct ToneMappingConsts {
            float exposure; int toneMapper; int gamma2; float pad1;
            LPMConsts lpmConsts;
        };
    };
}