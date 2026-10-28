#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include "offline_assets.hpp"
#include "dispatch.hpp"
#include <vector>

namespace jd::offline {
std::filesystem::path PackageRoot() {
    HMODULE module=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&PackageRoot),&module);
    wchar_t path[32768]{};
    GetModuleFileNameW(module,path,32768);
    return std::filesystem::path(path).parent_path()/L"JobDispatcherSP";
}
bool Available() {
    const auto root=PackageRoot();
    std::error_code error;
    return std::filesystem::is_regular_file(root/L"runtime/python/python.exe",error) &&
        std::filesystem::is_regular_file(root/L"runtime/converter_pix.exe",error) &&
        std::filesystem::is_regular_file(root/L"tools/offline-bootstrap.py",error);
}
std::filesystem::path GameDirectory() {
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
    return std::filesystem::path(path).parent_path().parent_path().parent_path();
}
std::filesystem::path GameHome() {
    int count=0;auto arguments=CommandLineToArgvW(GetCommandLineW(),&count);
    std::filesystem::path result;
    if(arguments){for(int i=1;i+1<count;++i)if(std::wstring(arguments[i])==L"-homedir"){result=arguments[i+1];break;}LocalFree(arguments);}
    if(!result.empty())return result/L"Euro Truck Simulator 2";
    PWSTR documents=nullptr;
    if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents,0,nullptr,&documents))){result=documents;CoTaskMemFree(documents);}
    return result/L"Euro Truck Simulator 2";
}
std::wstring Quote(const std::wstring& value) {
    std::wstring result=L"\"";size_t slashes=0;
    for(auto ch:value){
        if(ch==L'\\'){++slashes;continue;}
        if(ch==L'"')result.append(slashes*2+1,L'\\');else result.append(slashes,L'\\');
        slashes=0;result+=ch;
    }
    result.append(slashes*2,L'\\');return result+L'"';
}
bool Run(const std::filesystem::path& package,const std::filesystem::path& game,
         const std::filesystem::path& data,const std::filesystem::path& home,
         const std::function<bool()>& cancelled) {
    const auto python=package/L"runtime/python/python.exe";
    std::wstring command=Quote(python.wstring())+L" "+Quote((package/L"tools/offline-bootstrap.py").wstring())+
        L" --game "+Quote(game.wstring())+L" --data "+Quote(data.wstring())+
        L" --game-home "+Quote(home.wstring())+L" --converter "+Quote((package/L"runtime/converter_pix.exe").wstring());
    if(cancelled())return false;
    HANDLE job=CreateJobObjectW(nullptr,nullptr);
    if(!job)return false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){CloseHandle(job);return false;}
    STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION process{};
    if(!CreateProcessW(python.c_str(),command.data(),nullptr,nullptr,FALSE,
        CREATE_NO_WINDOW|CREATE_SUSPENDED|BELOW_NORMAL_PRIORITY_CLASS,nullptr,package.c_str(),&start,&process)){
        Log("Offline preparation could not start error=%lu",GetLastError());CloseHandle(job);return false;
    }
    if(!AssignProcessToJobObject(job,process.hProcess)){
        TerminateProcess(process.hProcess,1);CloseHandle(process.hThread);CloseHandle(process.hProcess);CloseHandle(job);return false;
    }
    ResumeThread(process.hThread);CloseHandle(process.hThread);
    bool completed=false;
    while(!cancelled()){
        if(WaitForSingleObject(process.hProcess,100)==WAIT_OBJECT_0){completed=true;break;}
    }
    DWORD code=1;if(completed)GetExitCodeProcess(process.hProcess,&code);
    CloseHandle(job); // Also terminates converter/parser descendants on cancellation.
    WaitForSingleObject(process.hProcess,5000);CloseHandle(process.hProcess);
    Log("Offline preparation completed=%d exit_code=%lu",completed,code);
    return completed&&code==0;
}
}
