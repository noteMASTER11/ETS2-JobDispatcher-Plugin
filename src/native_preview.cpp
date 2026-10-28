#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <imgui.h>
#include "preview.hpp"
#include "native_preview_abi.hpp"
#include "native_preview_policy.hpp"
#include "engine_memory.hpp"
#include "native_string.hpp"
#include "labels.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

namespace jd::preview {
#ifdef JD_NATIVE_PREVIEW_TEST
namespace testing { uintptr_t ModuleBase(); }
#endif
namespace {
// Only Frame/Shutdown own game objects. Present queues selection and displays
// an independently retained COM view. No file conversion runs in either path.
struct PreviewMailbox {
    std::mutex mutex;
    Request wanted;
    bool visible=false,failed=false;
    ULONGLONG selected_at=0;
    ID3D11Device* device=nullptr;
    ID3D11ShaderResourceView* published=nullptr;
    ID3D11ShaderResourceView* displayed=nullptr;
    unsigned device_epoch=0,camera_revision=0;
    float yaw=0,pitch=0,zoom=1;
} mailbox;
struct NativePreviewSession {
    uintptr_t root=0,widget=0,camera_config=0;
    NativePreviewPolicy policy;
    unsigned native_world=0,native_device_epoch=0,observed_world=0,camera_rendered=0;
    ULONGLONG assigned_at=0;
    bool native_submitted=false;
    uint16_t current_image=0xffff;
    uintptr_t current_resource=0;
    float original_positions[2][3]{};
} session;

template<class T> void Release(T*& object){if(object){object->Release();object=nullptr;}}
using engine::Read;
using engine::Write;
using engine::Readable;
#ifdef JD_NATIVE_PREVIEW_TEST
uintptr_t Base(){return testing::ModuleBase();}
#else
uintptr_t Base(){return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));}
#endif
bool AbiMatches(uintptr_t base){
    // Also require the widget's virtual dispatch table to match the mapped ABI.
    return Read<uintptr_t>(base+abi::widget_vtable+0x118)==base+abi::initialize&&
        Read<uintptr_t>(base+abi::widget_vtable+0x70)==base+abi::draw&&
        Read<uintptr_t>(base+abi::widget_vtable+0x1a0)==base+abi::assign_vehicle
#ifndef JD_NATIVE_PREVIEW_TEST
        &&
        Read<uint64_t>(base+abi::load_prefab)==0x41535520244c894cULL&&
        Read<uint64_t>(base+abi::create_srv)==0xec8348ec8b485540ULL
#endif
        ;
}
bool NativeContext(uintptr_t base){
    const auto index=Read<uint32_t>(base+0x2733298);
    return index<4&&Read<uintptr_t>(base+0x367f918+index*8)&&
        Read<uintptr_t>(base+abi::world_global)&&Read<uintptr_t>(base+abi::render_global);
}
void ClearPublished(){std::lock_guard<std::mutex> lock(mailbox.mutex);Release(mailbox.published);}
void DisposeUnit(uintptr_t unit){
    if(!unit)return;
    const auto dispose=Read<uintptr_t>(Read<uintptr_t>(unit)+8);
    reinterpret_cast<void(*)(uintptr_t)>(dispose)(unit);
    if(!(InterlockedDecrement(reinterpret_cast<LONG*>(unit+8))&0x1ffffff))
        reinterpret_cast<void(*)(uintptr_t)>(Base()+abi::free_unit)(unit);
}
void DestroyNative(){
    ClearPublished();
    DisposeUnit(session.camera_config);session.camera_config=0;
    if(session.root){
        // load_prefab returns an auto-owned unit. CreateNative converted its
        // ownership tag before adopting it; disposal now matches an owned unit.
        DisposeUnit(session.root);
    }
    session.root=session.widget=0;session.native_world=0;session.native_submitted=false;
    session.current_image=0xffff;session.current_resource=0;session.policy.Reset();
}
bool CreateNative(uintptr_t base,unsigned revision){
    const char* path="/ui/layout/vehicle_preview_widget.sii";
    reinterpret_cast<uintptr_t*(*)(uintptr_t*,const char**,uintptr_t,uintptr_t*)>(base+abi::load_prefab)(&session.root,&path,0,nullptr);
    if(!session.root)return false;
    InterlockedXor(reinterpret_cast<LONG*>(session.root+8),0x60000000);
    session.native_world=revision;
    if(Read<uintptr_t>(session.root)!=base+abi::window_vtable)return false;
    session.widget=reinterpret_cast<uintptr_t(*)(uintptr_t,uint64_t)>(base+abi::find_control)(session.root,1);
    if(!Readable(session.widget,0x3d8)||Read<uintptr_t>(session.widget)!=base+abi::widget_vtable)return false;
    // This prefab is deliberately NOT registered in the game's UI manager.
    // Its handler is null. Window initialization installs the child's owned
    // window link, required by the native camera's viewport calculation.
    const char* config="/def/job_company_trailer_preview.sii";
    if(!engine::AssignString(session.widget+abi::config_path_string,config,{base+abi::string_vtable,base+abi::set_string}))return false;
    Write<uint32_t>(session.root+0xd8,0x94);
    const auto initialize_window=Read<uintptr_t>(Read<uintptr_t>(session.root)+0x60);
    reinterpret_cast<void(*)(uintptr_t)>(initialize_window)(session.root);
    if(Read<uintptr_t>(session.widget+0x58)!=session.root)return false;
    Write<float>(session.widget+0x18,0);Write<float>(session.widget+0x1c,512);
    Write<float>(session.widget+0x20,320);Write<float>(session.widget+0x24,0);
    const auto flags=Read<uint32_t>(session.widget+0x60);
    Write<uint32_t>(session.widget+0x60,(flags&~0x30000000u)|0x18000000u);
    if(!Read<uint8_t>(session.widget+0x68))reinterpret_cast<void(*)(uintptr_t)>(base+abi::initialize)(session.widget);
    if(!Read<uintptr_t>(session.widget+0x138)||!Read<uintptr_t>(session.widget+0x3d0))return false;
    Log("Native trailer preview created from stock vehicle widget and job camera, world=%u",revision);
    return true;
}
void CameraAspect(){
    const auto camera=Read<uintptr_t>(session.widget+0x140);
    if(camera){const auto aspect=Read<uintptr_t>(Read<uintptr_t>(camera)+0x80);
        reinterpret_cast<void(*)(uintptr_t,float,float)>(aspect)(camera,512.0f,320.0f);}
}
bool ApplyOrbit(uintptr_t base,float horizontal,float vertical,float distance){
    if(!session.camera_config){
        const auto source=Read<uintptr_t>(session.widget+0x138);
        const auto clone=Read<uintptr_t>(Read<uintptr_t>(source)+0x10);
        if(!clone)return false;
        session.camera_config=reinterpret_cast<uintptr_t(*)(uintptr_t)>(clone)(source);
        if(!session.camera_config)return false;
        InterlockedIncrement(reinterpret_cast<LONG*>(session.camera_config+8));
        InterlockedOr(reinterpret_cast<LONG*>(session.camera_config+8),0x20000000);
        for(unsigned i=0;i<2;++i)for(unsigned axis=0;axis<3;++axis)
            session.original_positions[i][axis]=Read<float>(session.camera_config+0x5c+i*0x1c+axis*4);
    }
    for(unsigned i=0;i<2;++i){
        const auto offset=session.camera_config+0x5c+i*0x1c;
        float aim[3];for(unsigned axis=0;axis<3;++axis)aim[axis]=Read<float>(offset+12+axis*4);
        const auto* p=session.original_positions[i];
        const float dx=p[0]-aim[0],dy=p[1]-aim[1],dz=p[2]-aim[2];
        const float radius=std::sqrt(dx*dx+dy*dy+dz*dz)*distance;
        const float azimuth=std::atan2(dx,dz)+horizontal;
        const float elevation=std::clamp(std::atan2(dy,std::sqrt(dx*dx+dz*dz))+vertical,-1.3f,1.3f);
        Write<float>(offset,aim[0]+radius*std::cos(elevation)*std::sin(azimuth));
        Write<float>(offset+4,aim[1]+radius*std::sin(elevation));
        Write<float>(offset+8,aim[2]+radius*std::cos(elevation)*std::cos(azimuth));
    }
    // Rebuild the native camera arrays from our own configuration clone. Shared
    // game definitions and the truck's driving camera are never modified.
    reinterpret_cast<void(*)(uintptr_t,uintptr_t)>(base+abi::configure_camera)(session.widget,session.camera_config);
    reinterpret_cast<void(*)(uintptr_t,uint32_t)>(base+abi::select_camera)(session.widget,Read<uint32_t>(session.widget+0x1ec));
    CameraAspect();Write<uint8_t>(session.widget+0x374,0);session.native_submitted=false;
    return true;
}
ID3D11ShaderResourceView* TextureView(uintptr_t base,ID3D11Device* expected,uint16_t& image_id,uintptr_t& resource){
    const auto renderer=Read<uintptr_t>(base+abi::render_global);
    const auto graphics=Read<uintptr_t>(base+abi::graphics_global);
    // The pool below belongs specifically to dx11_device_t.
    if(!graphics||Read<ID3D11Device*>(graphics+abi::d3d_device)!=expected)return nullptr;
    const auto target_index=Read<uint32_t>(session.widget+0x370);
    const auto target_count=Read<uint64_t>(renderer+0x830);
    if(target_index==0xffffffff||target_count>1048576||target_index>=target_count)return nullptr;
    const auto target=Read<uintptr_t>(Read<uintptr_t>(renderer+0x828)+target_index*8);
    if(!target)return nullptr;
    const unsigned texture_id=Read<uint16_t>(target+0x148)&0x3fff;
    const auto texture_count=Read<uint64_t>(graphics+abi::texture_count);
    if(texture_id==0x3fff||texture_count>1048576||texture_id>=texture_count)return nullptr;
    const auto texture_table=Read<uintptr_t>(graphics+abi::texture_pool);
    if(!texture_table)return nullptr;
    const auto descriptor=texture_table+texture_id*0x80;
    image_id=Read<uint16_t>(descriptor+0x7c);
    const auto image_count=Read<uint64_t>(graphics+abi::image_count);
    if(image_id==0xffff||image_count>1048576||image_id>=image_count)return nullptr;
    const auto image_table=Read<uintptr_t>(graphics+abi::image_pool);
    if(!image_table)return nullptr;
    const auto image=image_table+image_id*0xa8;
    resource=Read<uintptr_t>(image+0x58);
    const auto type=Read<int>(image+0x1c);
    if(!resource||type!=1)return nullptr;
    if(image_id==session.current_image&&resource==session.current_resource)return nullptr;
    return reinterpret_cast<ID3D11ShaderResourceView*(*)(uintptr_t,uint16_t,int,uint64_t)>(base+abi::create_srv)(graphics,image_id,type,0);
}
void Fail(const char* reason,const Request& selection){
    Log("Native trailer preview unavailable: %s",reason);
    DestroyNative();std::lock_guard<std::mutex> lock(mailbox.mutex);
    if(mailbox.visible&&SameNativePreview(mailbox.wanted,selection))mailbox.failed=true;
}
} // namespace

void Suspend(){std::lock_guard<std::mutex> lock(mailbox.mutex);mailbox.visible=false;Release(mailbox.published);Release(mailbox.displayed);}
void ReleaseGpu(){
    std::lock_guard<std::mutex> lock(mailbox.mutex);
    mailbox.visible=false;Release(mailbox.published);Release(mailbox.displayed);Release(mailbox.device);mailbox.failed=false;++mailbox.device_epoch;
}
void Shutdown(){DestroyNative();ReleaseGpu();}
void QueueOrbit(float horizontal,float vertical,float wheel){
    std::lock_guard<std::mutex> lock(mailbox.mutex);
    mailbox.yaw+=horizontal;mailbox.pitch=std::clamp(mailbox.pitch+vertical,-1.1f,1.1f);
    mailbox.zoom=std::clamp(mailbox.zoom*std::exp(-wheel*.1f),.45f,2.0f);++mailbox.camera_revision;
}

namespace {
struct FrameCommand {
    Request selection;
    bool active=false,unavailable=false;
    ULONGLONG changed=0;
    unsigned epoch=0,camera_version=0;
    float horizontal=0,vertical=0,distance=1;
    ID3D11Device* expected=nullptr;
};
FrameCommand ReadCommand(){
    std::lock_guard<std::mutex> lock(mailbox.mutex);
    FrameCommand command{mailbox.wanted,mailbox.visible,mailbox.failed,mailbox.selected_at,
        mailbox.device_epoch,mailbox.camera_revision,mailbox.yaw,mailbox.pitch,mailbox.zoom,mailbox.device};
    if(command.expected)command.expected->AddRef();
    return command;
}
bool DrivingView(const FrameCommand& command){
    const auto telemetry=ReadTelemetry();
    return command.active&&command.expected&&WorldLoaded()&&telemetry.driving&&telemetry.placement_valid;
}
bool PrepareSession(FrameCommand& command,unsigned revision){
    if(session.observed_world!=revision){
        session.observed_world=revision;command.unavailable=false;
        std::lock_guard<std::mutex> lock(mailbox.mutex);mailbox.failed=false;
    }
    if(!DrivingView(command)){if(session.root)DestroyNative();return false;}
    if(session.root&&(session.native_world!=revision||session.native_device_epoch!=command.epoch))DestroyNative();
    return true;
}
bool SelectionReady(const FrameCommand& command){
    return !command.unavailable&&command.selection.cargo&&command.selection.source&&GetTickCount64()-command.changed>=100;
}
bool EnsureContext(uintptr_t base,const Request& selection){
    // Reuses catalogue allocations. Supported includes the exact EXE hash.
    static Snapshot snapshot;UpdateSnapshot(snapshot);
    if(!snapshot.supported)return false;
    if(!AbiMatches(base)){Fail("executable ABI mismatch",selection);return false;}
    if(NativeContext(base))return true;
    static ULONGLONG next_log=0;
    if(GetTickCount64()>=next_log){
        next_log=GetTickCount64()+10000;
        Log("Native trailer preview awaiting render context type=%u world=%p renderer=%p",Read<uint32_t>(base+0x2733298),
            reinterpret_cast<void*>(Read<uintptr_t>(base+abi::world_global)),reinterpret_cast<void*>(Read<uintptr_t>(base+abi::render_global)));
    }
    return false;
}
bool EnsureWidget(uintptr_t base,const FrameCommand& command,unsigned revision){
    if(session.root)return true;
    if(!CreateNative(base,revision)){Fail("stock vehicle widget could not be initialized",command.selection);return false;}
    session.native_device_epoch=command.epoch;return true;
}
bool AssignVehicle(uintptr_t base,const FrameCommand& command,unsigned revision,bool& assigned){
    if(!session.policy.NeedsVehicle(command.selection,revision))return true;
    const auto vehicle=BuildPreviewVehicle(command.selection);
    if(!vehicle.unit){Fail("live trailer prototype is unavailable for this selection",command.selection);return false;}
    ClearPublished();session.current_image=0xffff;session.current_resource=0;
    reinterpret_cast<void(*)(uintptr_t,uintptr_t,bool,const uint64_t*)>(base+abi::assign_vehicle)(session.widget,vehicle.unit,false,&vehicle.paint);
    DisposePreviewVehicle(vehicle.unit);
    session.policy.Accept(command.selection,revision);session.native_submitted=false;session.assigned_at=GetTickCount64();
    assigned=true;
    Log("Native trailer preview assigned cargo=%p trailer=%p source=%p setup_ms=%llu",
        reinterpret_cast<void*>(command.selection.cargo),reinterpret_cast<void*>(command.selection.trailer),
        reinterpret_cast<void*>(command.selection.source),session.assigned_at-command.changed);
    return true;
}
void ApplyCamera(uintptr_t base,const FrameCommand& command){
    if(session.camera_config||command.horizontal||command.vertical||command.distance!=1)
        ApplyOrbit(base,command.horizontal,command.vertical,command.distance);
    session.camera_rendered=command.camera_version;
}
bool AdvanceVehicle(uintptr_t base,const FrameCommand& command,bool assigned){
    const bool changed_camera=session.camera_rendered!=command.camera_version;
    if(session.current_resource&&!changed_camera&&!assigned)return false;
    // Advance the vehicle base, not the UI wrapper which processes menu input.
    reinterpret_cast<void(*)(uintptr_t)>(base+abi::update)(session.widget);
    if(changed_camera||assigned)ApplyCamera(base,command);
    CameraAspect();
    if(Read<uint8_t>(session.widget+0x374))return session.native_submitted;
    alignas(16) unsigned char frame[0x500]{};
    reinterpret_cast<void(*)(uintptr_t,void*,void*)>(base+abi::draw)(session.widget,nullptr,frame);
    // Commands become visible only on the next callback after game submission.
    session.native_submitted=Read<uint8_t>(session.widget+0x374)!=0;
    return false;
}
void PublishTexture(uintptr_t base,const FrameCommand& command,unsigned revision){
    uint16_t image_id=0xffff;uintptr_t resource=0;
    auto* view=TextureView(base,command.expected,image_id,resource);
    if(!view)return;
    {std::lock_guard<std::mutex> lock(mailbox.mutex);
        if(mailbox.visible&&mailbox.device==command.expected&&SameNativePreview(mailbox.wanted,command.selection)&&AssetsWorldRevision()==revision){
            Release(mailbox.published);mailbox.published=view;view=nullptr;
            session.current_image=image_id;session.current_resource=resource;
        }}
    if(view){view->Release();return;}
    Log("Native trailer preview ready image=%u render_ms=%llu total_ms=%llu",image_id,
        GetTickCount64()-session.assigned_at,GetTickCount64()-command.changed);
}
}
void Frame(){
    auto command=ReadCommand();
    struct DeviceRef{ID3D11Device* p;~DeviceRef(){if(p)p->Release();}} device_ref{command.expected};
    const auto revision=AssetsWorldRevision();
    if(!PrepareSession(command,revision)||!SelectionReady(command))return;
    const auto base=Base();
    if(!EnsureContext(base,command.selection)||!EnsureWidget(base,command,revision))return;
    bool assigned=false;
    if(AssignVehicle(base,command,revision,assigned)&&AdvanceVehicle(base,command,assigned))PublishTexture(base,command,revision);
}

void DrawNative(ID3D11Device* draw_device,const Request& selection,float width,float height,bool interactive){
    ID3D11ShaderResourceView* view=nullptr;bool unavailable=false;
    {std::lock_guard<std::mutex> lock(mailbox.mutex);
        if(mailbox.device!=draw_device){Release(mailbox.published);Release(mailbox.device);mailbox.device=draw_device;if(mailbox.device)mailbox.device->AddRef();mailbox.failed=false;++mailbox.device_epoch;}
        if(!mailbox.visible||!SameNativePreview(mailbox.wanted,selection)){
            mailbox.wanted=selection;mailbox.selected_at=GetTickCount64();mailbox.failed=false;Release(mailbox.published);
            mailbox.yaw=mailbox.pitch=0;mailbox.zoom=1;++mailbox.camera_revision;
        }
        mailbox.visible=true;unavailable=mailbox.failed;Release(mailbox.displayed);mailbox.displayed=mailbox.published;
        if(mailbox.displayed)mailbox.displayed->AddRef();view=mailbox.displayed;
    }
    const auto canvas=ImGui::GetCursorScreenPos();
    // A real item consumes dragging here, so ImGui cannot drag the window when
    // the cursor is over the preview. Native camera input is queued separately.
    ImGui::InvisibleButton("##model-orbit",ImVec2(width,height),ImGuiButtonFlags_MouseButtonLeft);
    if(view){
        ImGui::GetWindowDrawList()->AddImage(reinterpret_cast<ImTextureID>(view),canvas,ImVec2(canvas.x+width,canvas.y+height));
    }else{
        const auto text=labels::Ui(unavailable?"garageman_unavailable":"loading");
        ImGui::GetWindowDrawList()->AddText(ImVec2(canvas.x+12,canvas.y+12),ImGui::GetColorU32(ImGuiCol_TextDisabled),text.c_str());
    }
    if(interactive&&(ImGui::IsItemHovered()||ImGui::IsItemActive())){
        auto& io=ImGui::GetIO();
        const bool drag=ImGui::IsItemActive()&&ImGui::IsMouseDragging(ImGuiMouseButton_Left,0);
        if(drag||io.MouseWheel)QueueOrbit(drag?io.MouseDelta.x*.008f:0,drag?io.MouseDelta.y*.008f:0,io.MouseWheel);
    }
}
} // namespace jd::preview
