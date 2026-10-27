#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <compressapi.h>
#include <imgui.h>
#include "preview.hpp"
#include "dispatch.hpp"
#include "labels.hpp"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace jd::preview {
namespace {
struct Vertex { float position[3], normal[3], color[3], uv[2]; };
static_assert(sizeof(Vertex) == 44);
struct Batch { uint32_t first, count, material; };
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<Batch> batches;
    std::vector<std::vector<unsigned char>> textures;
    float center[3]{}, extent[3]{}, radius=1;
};
std::mutex cache_mutex;
std::string requested, processed, message = "loading";
std::vector<unsigned char> vertex_bytecode,pixel_bytecode;
std::atomic<bool> shaders_ready{false};
bool shaders_attempted=false;
std::shared_ptr<const Mesh> ready;
std::map<std::string, std::string> index;
bool indexed = false;
ID3D11Device* gpu_device = nullptr;
ID3D11DeviceContext1* gpu_context = nullptr;
ID3DDeviceContextState* gpu_state = nullptr;
ID3D11VertexShader* vs = nullptr;
ID3D11PixelShader* ps = nullptr;
ID3D11InputLayout* layout = nullptr;
ID3D11Buffer* vertices = nullptr;
ID3D11Buffer* constants = nullptr;
ID3D11Texture2D* texture = nullptr;
ID3D11RenderTargetView* target = nullptr;
ID3D11ShaderResourceView* image = nullptr;
ID3D11Texture2D* depth = nullptr;
ID3D11DepthStencilView* depth_view = nullptr;
ID3D11RasterizerState* raster = nullptr;
ID3D11DepthStencilState* depth_state = nullptr;
ID3D11SamplerState* sampler = nullptr;
ID3D11ShaderResourceView* white = nullptr;
std::vector<ID3D11ShaderResourceView*> material_views;
std::shared_ptr<const Mesh> uploaded;
float yaw = .72f, pitch = -.24f, zoom = 1;
bool dirty = true, gpu_failed = false;
template<class T> void Release(T*& object) { if (object) { object->Release(); object = nullptr; } }
const char shader[] = R"(
cbuffer View : register(b0) { float4 right; float4 up; float4 forward; float4 center; };
struct Input { float3 p:POSITION; float3 n:NORMAL; float3 c:COLOR; float2 uv:TEXCOORD0; };
struct Output { float4 p:SV_POSITION; float3 n:NORMAL; float3 c:COLOR; float2 uv:TEXCOORD0; };
Output vertex_main(Input v) { Output o; float3 p=v.p-center.xyz;
 o.p=float4(dot(p,right.xyz)*center.w*right.w,dot(p,up.xyz)*center.w,.5+dot(p,forward.xyz)*forward.w,1);
 o.n=v.n; o.c=v.c; o.uv=v.uv; return o; }
Texture2D albedo : register(t0); SamplerState surface_sampler : register(s0);
float4 pixel_main(Output v):SV_TARGET {
 float4 surface=albedo.Sample(surface_sampler,v.uv);clip(surface.a-.08);
 float light=.28+.72*abs(dot(normalize(v.n),normalize(float3(.35,.8,-.4))));
 return float4(surface.rgb*v.c*light,1); }
)";
uint32_t Word(const std::vector<unsigned char>& bytes,size_t offset) {
    uint32_t value=0;if(offset+4<=bytes.size())std::memcpy(&value,bytes.data()+offset,4);return value;
}
ID3D11ShaderResourceView* DdsTexture(ID3D11Device* device,const std::vector<unsigned char>& bytes) {
    if(bytes.size()<128||std::memcmp(bytes.data(),"DDS ",4))return nullptr;
    UINT height=Word(bytes,12),width=Word(bytes,16),mips=std::max(1u,Word(bytes,28));
    if(!width||!height||width>8192||height>8192||mips>16)return nullptr;
    const auto code=Word(bytes,84);DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;size_t offset=128;unsigned block=0,bpp=4;
    if(code==0x30315844) { // DX10
        if(bytes.size()<148||Word(bytes,132)!=3||Word(bytes,140)!=1)return nullptr;
        format=static_cast<DXGI_FORMAT>(Word(bytes,128));offset=148;
    } else if(code==0x31545844)format=DXGI_FORMAT_BC1_UNORM;
    else if(code==0x33545844||code==0x32545844)format=DXGI_FORMAT_BC2_UNORM;
    else if(code==0x35545844||code==0x34545844)format=DXGI_FORMAT_BC3_UNORM;
    else if(code==0x31495441||code==0x55344342)format=DXGI_FORMAT_BC4_UNORM;
    else if(code==0x32495441||code==0x55354342)format=DXGI_FORMAT_BC5_UNORM;
    else if(Word(bytes,88)==32)format=Word(bytes,92)==0xff?DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_B8G8R8A8_UNORM;
    else if(Word(bytes,88)==8){format=DXGI_FORMAT_R8_UNORM;bpp=1;}
    switch(format) {
    case DXGI_FORMAT_BC1_UNORM:case DXGI_FORMAT_BC1_UNORM_SRGB:case DXGI_FORMAT_BC4_UNORM:case DXGI_FORMAT_BC4_SNORM:block=8;break;
    case DXGI_FORMAT_BC2_UNORM:case DXGI_FORMAT_BC2_UNORM_SRGB:case DXGI_FORMAT_BC3_UNORM:case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC5_UNORM:case DXGI_FORMAT_BC5_SNORM:case DXGI_FORMAT_BC6H_UF16:case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_UNORM:case DXGI_FORMAT_BC7_UNORM_SRGB:block=16;break;
    case DXGI_FORMAT_R8_UNORM:bpp=1;break;
    case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM:case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:case DXGI_FORMAT_B8G8R8X8_UNORM:break;
    default:return nullptr;
    }
    std::vector<D3D11_SUBRESOURCE_DATA> initial;
    UINT w=width,h=height;
    for(UINT mip=0;mip<mips;++mip) {
        const UINT pitch=block?std::max(1u,(w+3)/4)*block:w*bpp;
        const size_t size=pitch*(block?std::max(1u,(h+3)/4):h);
        if(offset+size>bytes.size())return nullptr;
        initial.push_back({bytes.data()+offset,pitch,0});offset+=size;w=std::max(1u,w/2);h=std::max(1u,h/2);
    }
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=mips;desc.ArraySize=1;
    desc.Format=format;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* texture=nullptr;ID3D11ShaderResourceView* view=nullptr;
    if(SUCCEEDED(device->CreateTexture2D(&desc,initial.data(),&texture)))device->CreateShaderResourceView(texture,nullptr,&view);
    Release(texture);return view;
}
bool CreateGpu(ID3D11Device* device, ID3D11DeviceContext* context) {
    if (gpu_device) return gpu_device == device;
    ID3D11Device1* device1 = nullptr;
    if (FAILED(device->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&device1)))) return false;
    if (FAILED(context->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&gpu_context)))) { Release(device1); return false; }
    const D3D_FEATURE_LEVEL levels[]{device->GetFeatureLevel()};
    const UINT flags = device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED
        ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;
    const auto state_result = device1->CreateDeviceContextState(flags, levels, 1, D3D11_SDK_VERSION,
        __uuidof(ID3D11Device), nullptr, &gpu_state);
    Release(device1);
    if (FAILED(state_result)) return false;
    gpu_device = device; gpu_device->AddRef();
    HRESULT result=device->CreateVertexShader(vertex_bytecode.data(),vertex_bytecode.size(),nullptr,&vs);
    if(SUCCEEDED(result))result=device->CreatePixelShader(pixel_bytecode.data(),pixel_bytecode.size(),nullptr,&ps);
    const D3D11_INPUT_ELEMENT_DESC elements[]{
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,36,D3D11_INPUT_PER_VERTEX_DATA,0}};
    if(SUCCEEDED(result))result=device->CreateInputLayout(elements,4,vertex_bytecode.data(),vertex_bytecode.size(),&layout);
    D3D11_BUFFER_DESC cb{}; cb.ByteWidth = 64; cb.Usage = D3D11_USAGE_DEFAULT; cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (SUCCEEDED(result)) result = device->CreateBuffer(&cb, nullptr, &constants);
    D3D11_TEXTURE2D_DESC td{}; td.Width=512; td.Height=320; td.MipLevels=td.ArraySize=1;
    td.Format=DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_DEFAULT;
    td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    if (SUCCEEDED(result)) result=device->CreateTexture2D(&td,nullptr,&texture);
    if (SUCCEEDED(result)) result=device->CreateRenderTargetView(texture,nullptr,&target);
    if (SUCCEEDED(result)) result=device->CreateShaderResourceView(texture,nullptr,&image);
    td.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;td.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    if (SUCCEEDED(result)) result=device->CreateTexture2D(&td,nullptr,&depth);
    if (SUCCEEDED(result)) result=device->CreateDepthStencilView(depth,nullptr,&depth_view);
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
    if (SUCCEEDED(result)) result=device->CreateRasterizerState(&rd,&raster);
    D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
    if (SUCCEEDED(result)) result=device->CreateDepthStencilState(&ds,&depth_state);
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sd.MaxLOD=D3D11_FLOAT32_MAX;
    if(SUCCEEDED(result))result=device->CreateSamplerState(&sd,&sampler);
    const UINT pixel=0xffffffff;D3D11_SUBRESOURCE_DATA one{&pixel,4,0};
    D3D11_TEXTURE2D_DESC wd{};wd.Width=wd.Height=wd.MipLevels=wd.ArraySize=1;wd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    wd.SampleDesc.Count=1;wd.Usage=D3D11_USAGE_IMMUTABLE;wd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* wt=nullptr;
    if(SUCCEEDED(result))result=device->CreateTexture2D(&wd,&one,&wt);
    if(SUCCEEDED(result))result=device->CreateShaderResourceView(wt,nullptr,&white);
    Release(wt);
    return SUCCEEDED(result);
}
void Render(const Mesh& mesh) {
    struct View { float right[4], up[4], forward[4], center[4]; } view{};
    const float sy=std::sin(yaw),cy=std::cos(yaw),sp=std::sin(pitch),cp=std::cos(pitch);
    view.right[0]=cy;view.right[2]=-sy;view.right[3]=320.0f/512.0f;
    view.up[0]=-sp*sy;view.up[1]=cp;view.up[2]=-sp*cy;
    view.forward[0]=sy*cp;view.forward[1]=sp;view.forward[2]=cy*cp;view.forward[3]=.45f/mesh.radius;
    const float horizontal=std::abs(cy)*mesh.extent[0]+std::abs(sy)*mesh.extent[2];
    const float vertical=std::abs(sp*sy)*mesh.extent[0]+std::abs(cp)*mesh.extent[1]+std::abs(sp*cy)*mesh.extent[2];
    std::copy(mesh.center,mesh.center+3,view.center);
    view.center[3]=zoom*.88f/std::max(.01f,std::max(horizontal*view.right[3],vertical));
    ID3DDeviceContextState* previous=nullptr;
    gpu_context->SwapDeviceContextState(gpu_state,&previous);
    gpu_context->UpdateSubresource(constants,0,nullptr,&view,0,0);
    const float clear[]{.045f,.065f,.09f,1};gpu_context->ClearRenderTargetView(target,clear);
    gpu_context->ClearDepthStencilView(depth_view,D3D11_CLEAR_DEPTH,1,0);
    gpu_context->OMSetRenderTargets(1,&target,depth_view);gpu_context->OMSetDepthStencilState(depth_state,0);
    gpu_context->RSSetState(raster);D3D11_VIEWPORT viewport{0,0,512,320,0,1};gpu_context->RSSetViewports(1,&viewport);
    UINT stride=sizeof(Vertex),offset=0;gpu_context->IASetVertexBuffers(0,1,&vertices,&stride,&offset);
    gpu_context->IASetInputLayout(layout);gpu_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    gpu_context->VSSetShader(vs,nullptr,0);gpu_context->VSSetConstantBuffers(0,1,&constants);
    gpu_context->PSSetShader(ps,nullptr,0);gpu_context->PSSetSamplers(0,1,&sampler);
    for(const auto& batch:mesh.batches) {
        auto* view=batch.material<material_views.size()&&material_views[batch.material]?material_views[batch.material]:white;
        gpu_context->PSSetShaderResources(0,1,&view);gpu_context->Draw(batch.count,batch.first);
    }
    gpu_context->SwapDeviceContextState(previous,nullptr);Release(previous);
    dirty=false;
}
}
void Pump() {
    if(!shaders_attempted){
        shaders_attempted=true;
        ID3DBlob *code=nullptr,*errors=nullptr;
        auto compile=[&](const char* entry,const char* target,std::vector<unsigned char>& bytes){
            const auto result=D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,entry,target,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
            Release(errors);
            if(SUCCEEDED(result)){const auto* data=static_cast<unsigned char*>(code->GetBufferPointer());bytes.assign(data,data+code->GetBufferSize());}
            Release(code);return SUCCEEDED(result);
        };
        const bool valid=compile("vertex_main","vs_4_0",vertex_bytecode)&&compile("pixel_main","ps_4_0",pixel_bytecode);
        shaders_ready.store(valid,std::memory_order_release);
        Log("Preview shaders prepared in background valid=%d",valid);
    }
    if (!indexed) {
        std::map<std::string,std::string> loaded_index;
        std::ifstream file(std::filesystem::path(DataDirectory())/L"previews/index.tsv");
        std::string line;
        while (std::getline(file,line)) {
            const auto first=line.find('\t'),second=line.find('\t',first+1);
            if(first==std::string::npos)continue;
            const auto name=line.substr(first+1,second-first-1);
            if(name.find_first_not_of("0123456789abcdef.jdm")!=std::string::npos||name.find("..")!=std::string::npos)continue;
            loaded_index.emplace(line.substr(0,first),name);
        }
        {std::lock_guard<std::mutex> lock(cache_mutex);index.swap(loaded_index);}
        indexed=true;
        Log("Preview index loaded models=%llu directory=%ls",static_cast<unsigned long long>(index.size()),DataDirectory().c_str());
    }
    std::string token;
    { std::lock_guard<std::mutex> lock(cache_mutex);if(requested==processed)return;token=requested;processed=token;ready.reset(); }
    std::shared_ptr<Mesh> mesh;
    std::string status="garageman_unavailable";
    const auto found=index.find(token);
    Log("Preview requested token=%s indexed=%d",token.c_str(),found!=index.end());
    if(found!=index.end()) {
        std::ifstream file(std::filesystem::path(DataDirectory())/L"previews"/found->second,std::ios::binary);
        char magic[4]{};uint32_t expanded=0;file.read(magic,4);file.read(reinterpret_cast<char*>(&expanded),4);
        if(file&&std::string(magic,4)=="JDC2"&&expanded>=16&&expanded<=64*1024*1024) {
            std::vector<unsigned char> decoded(expanded);
            file.seekg(0,std::ios::end);const auto size=file.tellg();file.seekg(8);
            bool valid=size>8&&size<=32*1024*1024;
            if(valid) {
                std::vector<char> bytes(static_cast<size_t>(size)-8);file.read(bytes.data(),bytes.size());
                DECOMPRESSOR_HANDLE decoder=nullptr;SIZE_T restored=0;
                valid=CreateDecompressor(COMPRESS_ALGORITHM_XPRESS_HUFF,nullptr,&decoder)!=FALSE;
                if(valid)valid=Decompress(decoder,bytes.data(),bytes.size(),decoded.data(),decoded.size(),&restored)!=FALSE&&restored==decoded.size();
                if(decoder)CloseDecompressor(decoder);
            }
            const auto count=valid?Word(decoded,4):0,materials=valid?Word(decoded,8):0,batches=valid?Word(decoded,12):0;
            valid=valid&&std::memcmp(decoded.data(),"JDT1",4)==0&&count&&count<=600000&&count%3==0&&materials<=512&&batches<=4096;
            std::vector<std::string> textures;size_t offset=16;
            if(valid)for(uint32_t m=0;m<materials;++m) {
                uint16_t length=0;if(offset+2>decoded.size()){valid=false;break;}
                std::memcpy(&length,decoded.data()+offset,2);offset+=2;
                if(length>80||offset+length>decoded.size()){valid=false;break;}
                std::string name(reinterpret_cast<char*>(decoded.data()+offset),length);offset+=length;
                if(name.find("..")!=std::string::npos||name.find_first_not_of("0123456789abcdef.dds")!=std::string::npos){valid=false;break;}
                textures.push_back(std::move(name));
            }
            if(valid&&offset+size_t(batches)*sizeof(Batch)+size_t(count)*sizeof(Vertex)==decoded.size()) {
                mesh=std::make_shared<Mesh>();mesh->batches.resize(batches);mesh->vertices.resize(count);
                std::memcpy(mesh->batches.data(),decoded.data()+offset,size_t(batches)*sizeof(Batch));offset+=size_t(batches)*sizeof(Batch);
                std::memcpy(mesh->vertices.data(),decoded.data()+offset,size_t(count)*sizeof(Vertex));
                for(const auto& batch:mesh->batches)valid=valid&&batch.first<=count&&batch.count<=count-batch.first&&batch.material<materials;
            } else valid=false;
            if(valid&&mesh) {
                size_t texture_bytes=0;
                for(const auto& name:textures) {
                    std::vector<unsigned char> bytes;
                    if(!name.empty()) {
                        std::ifstream input(std::filesystem::path(DataDirectory())/L"previews"/name,std::ios::binary|std::ios::ate);
                        const auto length=input.tellg();
                        if(length>0&&length<16*1024*1024&&texture_bytes+static_cast<size_t>(length)<64*1024*1024) {
                            bytes.resize(static_cast<size_t>(length));input.seekg(0);input.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
                            texture_bytes+=bytes.size();
                        }
                    }
                    mesh->textures.push_back(std::move(bytes));
                }
                float minimum[3]{1e30f,1e30f,1e30f},maximum[3]{-1e30f,-1e30f,-1e30f};
                for(const auto& vertex:mesh->vertices)for(unsigned i=0;i<3;++i) {
                    valid=valid&&std::isfinite(vertex.position[i])&&std::abs(vertex.position[i])<10000;
                    minimum[i]=std::min(minimum[i],vertex.position[i]);maximum[i]=std::max(maximum[i],vertex.position[i]);
                }
                float radius2=0;for(unsigned i=0;i<3;++i){mesh->center[i]=(minimum[i]+maximum[i])*.5f;mesh->extent[i]=(maximum[i]-minimum[i])*.5f;radius2+=mesh->extent[i]*mesh->extent[i];}
                mesh->radius=std::max(.1f,std::sqrt(radius2));
                Log("Preview ready token=%s vertices=%u materials=%u texture_bytes=%llu",token.c_str(),count,materials,static_cast<unsigned long long>(texture_bytes));
            }
            if(!valid)mesh.reset();
            if(!mesh)Log("Preview rejected token=%s count=%u materials=%u batches=%u offset=%llu expanded=%u",token.c_str(),count,materials,batches,static_cast<unsigned long long>(offset),expanded);
            status=mesh?"cargo_model_t":"garageman_unavailable";
        }
    }
    { std::lock_guard<std::mutex> lock(cache_mutex);if(requested==token){ready=std::move(mesh);message=std::move(status);} }
}
void ReleaseGpu() {
    for(auto*& view:material_views)Release(view);material_views.clear();Release(sampler);Release(white);
    uploaded.reset();Release(vertices);Release(constants);Release(layout);Release(vs);Release(ps);
    Release(image);Release(target);Release(texture);Release(depth_view);Release(depth);Release(raster);Release(depth_state);
    Release(gpu_state);Release(gpu_context);Release(gpu_device);dirty=true;gpu_failed=false;
}
bool HasModel(const std::string& token){std::lock_guard<std::mutex> lock(cache_mutex);return index.find(token)!=index.end();}
void Draw(ID3D11Device* device,ID3D11DeviceContext* context,const std::string& token,float width,float height,bool interactive) {
    std::shared_ptr<const Mesh> mesh;std::string status;
    {std::lock_guard<std::mutex> lock(cache_mutex);if(requested!=token){requested=token;ready.reset();message="loading";}
        mesh=ready;status=message;}
    const bool compiled=shaders_ready.load(std::memory_order_acquire);
    if(mesh&&!gpu_failed&&compiled) {
        if(gpu_device&&gpu_device!=device)ReleaseGpu();
        if(!CreateGpu(device,context)){ReleaseGpu();gpu_failed=true;}
        else if(uploaded!=mesh) {
            for(auto*& view:material_views)Release(view);material_views.clear();
            for(const auto& bytes:mesh->textures)material_views.push_back(DdsTexture(device,bytes));
            Release(vertices);D3D11_BUFFER_DESC bd{};bd.ByteWidth=static_cast<UINT>(mesh->vertices.size()*sizeof(Vertex));
            bd.Usage=D3D11_USAGE_IMMUTABLE;bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA initial{mesh->vertices.data(),0,0};
            if(SUCCEEDED(device->CreateBuffer(&bd,&initial,&vertices))){uploaded=mesh;dirty=true;yaw=.72f;pitch=-.24f;zoom=1;}
            else gpu_failed=true;
        }
        if(vertices&&!gpu_failed) {
            if(dirty)Render(*mesh);
            const auto canvas=ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##model-orbit",ImVec2(width,height),ImGuiButtonFlags_MouseButtonLeft);
            ImGui::GetWindowDrawList()->AddImage(reinterpret_cast<ImTextureID>(image),canvas,ImVec2(canvas.x+width,canvas.y+height));
            if(interactive&&(ImGui::IsItemHovered()||ImGui::IsItemActive())) {
                auto& io=ImGui::GetIO();
                if(ImGui::IsItemActive()&&ImGui::IsMouseDragging(ImGuiMouseButton_Left,0)){yaw+=io.MouseDelta.x*.012f;pitch=std::clamp(pitch+io.MouseDelta.y*.012f,-1.25f,1.25f);dirty=true;}
                if(io.MouseWheel){zoom=std::clamp(zoom+io.MouseWheel*.1f,.5f,1.8f);dirty=true;}
            }
        }
    }
    if(!mesh||gpu_failed||!compiled){ImGui::BeginChild("preview-empty",ImVec2(width,height),ImGuiChildFlags_Borders);ImGui::TextWrapped("%s",labels::Ui(gpu_failed?"garageman_unavailable":status).c_str());ImGui::EndChild();}
}
}
