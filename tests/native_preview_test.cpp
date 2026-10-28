// Exercises the production preview queue, frame callback and COM lifetime on a
// synthetic engine layout and a real DX11 WARP device. Does not load ETS2.
#include <windows.h>
#include <d3d11.h>
#include <imgui.h>
#include "preview.hpp"
#include "native_preview_abi.hpp"
#include "native_string.hpp"
#include <cstring>
#include <array>
#include <cassert>
#include <iostream>
#include <vector>

namespace fixture {
uintptr_t module=0,graphics=0;
alignas(16) std::array<unsigned char,0x400> window{},control{},camera{},config{},private_config{},target{},descriptor{},image{};
uintptr_t camera_vtable[0x20]{};
uintptr_t config_vtable[3]{};
uintptr_t targets[1]{};
ID3D11Device* gpu=nullptr;
ID3D11DeviceContext* context=nullptr;
ID3D11Texture2D* texture=nullptr;
ID3D11RenderTargetView* rtv=nullptr;
std::vector<ID3D11ShaderResourceView*> views;
std::vector<char> config_text;
jd::Request selection;
unsigned builds=0,clones_disposed=0,assignments=0,renders=0,windows=0,disposed=0,freed=0,revision=1;
unsigned camera_changes=0,configs_disposed=0;
bool world=true,driving=true;
DWORD callback_thread=0;
template<class T> void Put(uintptr_t p,T v){std::memcpy(reinterpret_cast<void*>(p),&v,sizeof(v));}
template<class T> T Get(uintptr_t p){T v{};std::memcpy(&v,reinterpret_cast<void*>(p),sizeof(v));return v;}
void Commit(uintptr_t address){assert(VirtualAlloc(reinterpret_cast<void*>(address&~uintptr_t(4095)),4096,MEM_COMMIT,PAGE_EXECUTE_READWRITE));}
void Stub(uintptr_t rva,uintptr_t function){
    const auto address=module+rva;Commit(address);
    const unsigned char begin[]={0x48,0xb8};std::memcpy(reinterpret_cast<void*>(address),begin,2);
    Put(address+2,function);Put<uint16_t>(address+10,0xe0ff);
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(address),12);
}
void Callback(){assert(callback_thread==GetCurrentThreadId());}
void Dispose(uintptr_t unit){
    Callback();
    if(unit==reinterpret_cast<uintptr_t>(private_config.data())){++configs_disposed;return;}
    assert(unit==reinterpret_cast<uintptr_t>(window.data()));++disposed;
    Put<uintptr_t>(reinterpret_cast<uintptr_t>(control.data())+0x58,0);
}
void Free(uintptr_t unit){Callback();if(unit==reinterpret_cast<uintptr_t>(window.data()))++freed;}
uintptr_t CloneConfig(uintptr_t source){Callback();assert(source==reinterpret_cast<uintptr_t>(config.data()));private_config=config;Put<uint32_t>(reinterpret_cast<uintptr_t>(private_config.data())+8,0x80000000);return reinterpret_cast<uintptr_t>(private_config.data());}
void ConfigureCamera(uintptr_t widget,uintptr_t cfg){
    Callback();assert(widget==reinterpret_cast<uintptr_t>(control.data()));assert(cfg==reinterpret_cast<uintptr_t>(private_config.data()));
    assert(Get<float>(cfg+0x5c)!=Get<float>(reinterpret_cast<uintptr_t>(config.data())+0x5c));++camera_changes;
}
void SelectCamera(uintptr_t,uint32_t index){Callback();assert(index==0);}
void WindowInit(uintptr_t root){Callback();Put(reinterpret_cast<uintptr_t>(control.data())+0x58,root);}
uintptr_t* Load(uintptr_t* out,const char** path,uintptr_t,uintptr_t*){
    Callback();assert(std::string(*path)=="/ui/layout/vehicle_preview_widget.sii");++windows;
    window.fill(0);control.fill(0);camera.fill(0);config.fill(0);
    const auto root=reinterpret_cast<uintptr_t>(window.data()),widget=reinterpret_cast<uintptr_t>(control.data());
    Put(root,module+jd::preview::abi::window_vtable);Put<uint32_t>(root+8,0xe0000001);
    Put(widget,module+jd::preview::abi::widget_vtable);Put<uint32_t>(widget+0x370,0xffffffff);
    // Actual layout established by ETS2's constructor at 0x140ff2c00:
    // string object +0x118, char* +0x120, uint32 length/capacity +0x128/+0x12c.
    // The previous fixture had no string and accepted the incorrect char**.
    const std::string original="/def/compman_truck_preview.sii";
    config_text.assign(original.begin(),original.end());config_text.push_back(0);
    Put(widget+0x118,module+0x21d29f0);Put(widget+0x120,config_text.data());
    Put<uint32_t>(widget+0x128,static_cast<uint32_t>(original.size()));Put<uint32_t>(widget+0x12c,32);
    *out=root;return out;
}
uintptr_t Find(uintptr_t,uint64_t id){Callback();assert(id==1);return reinterpret_cast<uintptr_t>(control.data());}
uintptr_t SetString(uintptr_t field,const char** text){
    Callback();
    assert(Get<uintptr_t>(field)==module+0x21d29f0); // The receiver is the string object, not its buffer member.
    assert(Get<char*>(field+8)==config_text.data());
    assert(Get<uint32_t>(field+0x10)==std::strlen(config_text.data()));
    assert(std::string(*text)=="/def/job_company_trailer_preview.sii");
    config_text.assign(*text,*text+std::strlen(*text)+1);
    Put(field+8,config_text.data());Put<uint32_t>(field+0x10,static_cast<uint32_t>(config_text.size()-1));
    Put<uint32_t>(field+0x14,static_cast<uint32_t>(config_text.size()));
    return field;
}
void Aspect(uintptr_t,float w,float h){Callback();assert(w==512&&h==320);}
void Initialize(uintptr_t widget){
    Callback();assert(Get<uintptr_t>(widget+0x58));
    config_vtable[1]=reinterpret_cast<uintptr_t>(&Dispose);config_vtable[2]=reinterpret_cast<uintptr_t>(&CloneConfig);
    Put(reinterpret_cast<uintptr_t>(config.data()),reinterpret_cast<uintptr_t>(config_vtable));
    for(unsigned i=0;i<2;++i){const auto position=reinterpret_cast<uintptr_t>(config.data())+0x5c+i*0x1c;
        Put<float>(position,-9);Put<float>(position+4,2.5f);Put<float>(position+8,-19);Put<float>(position+16,2.4f);Put<float>(position+20,-1);}
    Put<uint8_t>(widget+0x68,1);Put(widget+0x138,reinterpret_cast<uintptr_t>(config.data()));
    Put(widget+0x140,reinterpret_cast<uintptr_t>(camera.data()));Put<uintptr_t>(widget+0x3d0,1);
    camera_vtable[0x80/8]=reinterpret_cast<uintptr_t>(&Aspect);Put(reinterpret_cast<uintptr_t>(camera.data()),reinterpret_cast<uintptr_t>(camera_vtable));
}
void Assign(uintptr_t widget,uintptr_t vehicle,bool clear,const uint64_t* paint){
    Callback();assert(vehicle==0x123456&& !clear&&*paint==selection.source);++assignments;
    Put<uint32_t>(widget+0x370,0);Put<uint8_t>(widget+0x374,0);
}
void Update(uintptr_t){Callback();}
void Draw(uintptr_t widget,void*,void* frame){
    Callback();assert(Get<uint32_t>(reinterpret_cast<uintptr_t>(frame)+0x3d4)==0);++renders;
    const float color[]={static_cast<float>(selection.cargo)/10,0,0,1};context->ClearRenderTargetView(rtv,color);
    Put<uint8_t>(widget+0x374,1);
}
ID3D11ShaderResourceView* View(uintptr_t,uint16_t id,int type,uint64_t options){
    Callback();assert(id==0&&type==1&&!options);
    ID3D11ShaderResourceView* view=nullptr;assert(SUCCEEDED(gpu->CreateShaderResourceView(texture,nullptr,&view)));
    view->AddRef();views.push_back(view);return view;
}
void Setup(){
    module=reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr,0x3800000,MEM_RESERVE,PAGE_NOACCESS));assert(module);
    graphics=reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr,0x3640000,MEM_RESERVE,PAGE_NOACCESS));assert(graphics);
    D3D_FEATURE_LEVEL level{};assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&gpu,&level,&context)));
    D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=64;td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    assert(SUCCEEDED(gpu->CreateTexture2D(&td,nullptr,&texture)));assert(SUCCEEDED(gpu->CreateRenderTargetView(texture,nullptr,&rtv)));
    namespace a=jd::preview::abi;
    for(const auto rva:{a::window_vtable,a::widget_vtable,a::render_global,a::graphics_global,a::world_global,uintptr_t(0x2733298),uintptr_t(0x367f918)})Commit(module+rva);
    for(const auto offset:{a::d3d_device,a::texture_pool,a::texture_count,a::image_pool,a::image_count})Commit(graphics+offset);
    Put(module+a::window_vtable+8,reinterpret_cast<uintptr_t>(&Dispose));Put(module+a::window_vtable+0x60,reinterpret_cast<uintptr_t>(&WindowInit));
    Put(module+a::widget_vtable+0x118,module+a::initialize);Put(module+a::widget_vtable+0x70,module+a::draw);Put(module+a::widget_vtable+0x1a0,module+a::assign_vehicle);
    Stub(a::load_prefab,reinterpret_cast<uintptr_t>(&Load));Stub(a::find_control,reinterpret_cast<uintptr_t>(&Find));
    Stub(a::set_string,reinterpret_cast<uintptr_t>(&SetString));Stub(a::initialize,reinterpret_cast<uintptr_t>(&Initialize));
    Stub(a::assign_vehicle,reinterpret_cast<uintptr_t>(&Assign));Stub(a::update,reinterpret_cast<uintptr_t>(&Update));
    Stub(a::draw,reinterpret_cast<uintptr_t>(&Draw));Stub(a::create_srv,reinterpret_cast<uintptr_t>(&View));Stub(a::free_unit,reinterpret_cast<uintptr_t>(&Free));
    Stub(a::configure_camera,reinterpret_cast<uintptr_t>(&ConfigureCamera));Stub(a::select_camera,reinterpret_cast<uintptr_t>(&SelectCamera));
    // A tiny render-target registry; unused bytes remain committed zeros.
    auto* renderer=VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(renderer);
    const auto rp=reinterpret_cast<uintptr_t>(renderer);Put(module+a::render_global,rp);
    targets[0]=reinterpret_cast<uintptr_t>(target.data());Put(rp+0x828,reinterpret_cast<uintptr_t>(targets));Put<uint64_t>(rp+0x830,1);
    Put(module+a::world_global,rp);Put<uint32_t>(module+0x2733298,0);Put(module+0x367f918,rp);
    Put(module+a::graphics_global,graphics);Put(graphics+a::d3d_device,gpu);
    Put(graphics+a::texture_pool,reinterpret_cast<uintptr_t>(descriptor.data()));Put<uint64_t>(graphics+a::texture_count,1);
    Put(graphics+a::image_pool,reinterpret_cast<uintptr_t>(image.data()));Put<uint64_t>(graphics+a::image_count,1);
    Put(reinterpret_cast<uintptr_t>(image.data())+0x58,texture);Put<int>(reinterpret_cast<uintptr_t>(image.data())+0x1c,1);
    callback_thread=GetCurrentThreadId();
}
void Gui(const jd::Request& request){
    const auto before=builds+assignments+renders+windows;
    ImGui::NewFrame();ImGui::Begin("Preview test");jd::preview::DrawNative(gpu,request,512,320,false);ImGui::End();ImGui::Render();
    assert(before==builds+assignments+renders+windows); // GUI never calls game functions.
}
void Prepare(const jd::Request& request){Gui(request);Sleep(115);jd::preview::Frame();jd::preview::Frame();}
}
namespace jd::preview::testing {uintptr_t ModuleBase(){return fixture::module;}}
namespace jd {
void Log(const char*,...){}
bool WorldLoaded(){return fixture::world;}
bool AssetsReady(){return false;} // Background translations may still be pending.
unsigned AssetsWorldRevision(){return fixture::revision;}
TelemetrySample ReadTelemetry(){TelemetrySample t;t.driving=fixture::driving;t.placement_valid=true;return t;}
void UpdateSnapshot(Snapshot& s){s.supported=true;s.world_ready=fixture::world;}
PreviewVehicle BuildPreviewVehicle(const Request& r){fixture::Callback();fixture::selection=r;++fixture::builds;return {0x123456,r.source};}
void DisposePreviewVehicle(uintptr_t p){fixture::Callback();assert(p==0x123456);++fixture::clones_disposed;}
}
namespace jd::labels {std::string Ui(const std::string& s){return s;}}

int main(){
    fixture::Setup();ImGui::CreateContext();
    auto& io=ImGui::GetIO();io.DisplaySize=ImVec2(900,600);io.DeltaTime=1.0f/60;
    io.IniFilename=nullptr;io.LogFilename=nullptr;
    unsigned char* pixels=nullptr;int w=0,h=0;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    jd::Request request;request.cargo=1;request.source=10;
    fixture::Prepare(request);assert(fixture::builds==1&&fixture::assignments==1&&fixture::views.size()==1);
    // The crashing receiver from the real report must be rejected before any
    // native write: +0x120 is char**, while string_t itself starts at +0x118.
    assert(!jd::engine::AssignString(reinterpret_cast<uintptr_t>(fixture::control.data())+0x120,"/def/job_company_trailer_preview.sii",
        {fixture::module+jd::preview::abi::string_vtable,fixture::module+jd::preview::abi::set_string}));
    fixture::Gui(request);for(int i=0;i<50;++i)jd::preview::Frame();assert(fixture::renders==1&&fixture::builds==1);
    jd::preview::QueueOrbit(.3f,0,2);
    jd::preview::Frame();jd::preview::Frame();assert(fixture::camera_changes==1&&fixture::renders==2&&fixture::builds==1);
    assert(fixture::views.size()==1); // Orbit redraws the existing native target.
    request.destination=50;request.take_immediately=false;fixture::Prepare(request);assert(fixture::builds==1);
    request.cargo=2;fixture::Gui(request);request.cargo=3;fixture::Prepare(request);
    assert(fixture::builds==2&&fixture::selection.cargo==3&&fixture::views.size()==2);
    assert(fixture::views[0]->AddRef()==2);fixture::views[0]->Release(); // Previous view only retained by the fixture.
    request.source=11;fixture::Prepare(request);assert(fixture::builds==3&&fixture::windows==1);
    fixture::Gui(request);jd::preview::Suspend();jd::preview::Frame();
    assert(fixture::disposed==1&&fixture::freed==1&&fixture::configs_disposed==1&&fixture::builds==fixture::clones_disposed);
    for(auto* view:fixture::views){assert(view->AddRef()==2);view->Release();}
    ++fixture::revision;fixture::Prepare(request);assert(fixture::windows==2&&fixture::builds==4);
    fixture::driving=false;jd::preview::Frame();assert(fixture::disposed==2);
    fixture::driving=true;fixture::Prepare(request);assert(fixture::windows==3);
    jd::preview::PrepareWindowClose();assert(fixture::disposed==3);
    fixture::Prepare(request);assert(fixture::windows==3); // Closing cannot recreate the scene.
    jd::preview::Shutdown();ImGui::DestroyContext();
    for(auto* view:fixture::views)view->Release();fixture::rtv->Release();fixture::texture->Release();fixture::context->Release();fixture::gpu->Release();
    std::cout<<"Native preview selection, debounce, paint, reuse, world reset and COM lifetime passed\n";
}
