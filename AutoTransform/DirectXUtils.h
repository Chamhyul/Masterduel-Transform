/*******************************************************************/
/*                                                                 */
/*  DirectXUtils.h                                                 */
/*                                                                 */
/*  DirectX 12 helper utilities for Premiere Pro GPU plugin.       */
/*  Supports shader loading from files and embedded memory.        */
/*                                                                 */
/*******************************************************************/

#pragma once

#if _WIN32

#include <d3d12.h>
#include <d3dcompiler.h>
#include <memory>
#include <wrl.h>
#include <string>

struct ShaderObject
{
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mPipelineState;
};
using ShaderObjectPtr = std::shared_ptr<ShaderObject>;

struct DXContext
{
    DXContext() = default;
    ~DXContext();

    // Initializes the DirectX Context
    bool Initialize(ID3D12Device *inDevice, ID3D12CommandQueue *inCommandQueue);

    // Creates a Shader Object from CSO and RootSignature files
    bool LoadShader(
        LPCWSTR inCSOPath, 
        LPCWSTR inRootSignaturePath, 
        ShaderObjectPtr &outShaderObject);

    // Creates a Shader Object directly from memory bytecode
    bool LoadShaderFromMemory(
        const void* inCSOData,
        size_t inCSOSize,
        const void* inRSData,
        size_t inRSSize,
        ShaderObjectPtr &outShaderObject);

    // Reserve Descriptor Heap Slots
    bool ReserveDescriptorHeapSlots(UINT inNumDescriptors, UINT &outHeapOffset);

    // Wait and Reset for next execution
    void CloseWaitAndReset();

    // Command Execution Objects
    Microsoft::WRL::ComPtr<ID3D12Device> mDevice;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCommandQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mCommandAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList;

    // Descriptor Heap Objects
    UINT mHeapOffset = 0;
    UINT mHandleSize = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE mCPUHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE mGPUHandle = {};
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDescriptorHeap;

    // Fence Objects
    UINT mFenceValue = 0;
    HANDLE mFenceEvent = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
};
using DXContextPtr = std::shared_ptr<DXContext>;

class DXShaderExecution
{
public:
    DXShaderExecution(
        const DXContextPtr &inContext,
        const ShaderObjectPtr &inShaderObject,
        const UINT inNumDescriptors);
    ~DXShaderExecution() = default;

    bool SetParamBuffer(void *inParamBuffer, UINT inParamBufferSize);
    bool SetUnorderedAccessView(ID3D12Resource *inBuffer, UINT inBufferSize);
    bool SetShaderResourceView(ID3D12Resource *inBuffer, UINT inBufferSize);

    bool Execute(const UINT inGridSizeX, const UINT inGridSizeY);

private:
    DXContextPtr mContext;
    ShaderObjectPtr mShaderObject;
    UINT mReservedDescriptorSlotBase;
    bool mIsValid = true;
    bool mIsUAVBaseSet = false;
    bool mIsSRVBaseSet = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> mHostParamBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> mDeviceParamBuffer;
};

bool GetShaderPath(
    const wchar_t* inModuleName,
    std::wstring& outCSOPath,
    std::wstring& outSigPath);

#endif // _WIN32
