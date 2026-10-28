#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include "offline_assets.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace jd { void Log(const char*,...){} }
void Check(bool value,const char* detail){if(!value)throw std::runtime_error(detail);}
DWORD Pid(const std::filesystem::path& file){DWORD pid=0;std::ifstream(file)>>pid;return pid;}
bool Exited(DWORD pid){HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,pid);if(!process)return GetLastError()==ERROR_INVALID_PARAMETER;bool result=WaitForSingleObject(process,2000)==WAIT_OBJECT_0;CloseHandle(process);return result;}
int wmain(int argc,wchar_t** argv){
 try{
    if(argc>2&&std::wstring(argv[1])==L"--home-check"){
        Check(jd::offline::GameHome()==std::filesystem::path(argv[2]),
              "GameHome must resolve the -homedir parent to the game's application directory");
        return 0;
    }
    if(argc>2&&std::wstring(argv[1])==L"--grandchild"){
        std::ofstream(std::filesystem::path(argv[2])/L"grandchild.pid")<<GetCurrentProcessId();Sleep(30000);return 0;
    }
    if(argc>2&&std::filesystem::path(argv[1]).filename()==L"offline-bootstrap.py"){
        std::filesystem::path data;bool cancel=false;
        for(int i=2;i+1<argc;++i){if(std::wstring(argv[i])==L"--data")data=argv[i+1];if(std::wstring(argv[i])==L"--game"&&std::wstring(argv[i+1])==L"cancel")cancel=true;}
        std::ofstream(data/L"child.pid")<<GetCurrentProcessId();
        if(cancel){
            wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);
            auto command=jd::offline::Quote(self)+L" --grandchild "+jd::offline::Quote(data.wstring());
            STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
            if(CreateProcessW(self,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)){CloseHandle(pi.hThread);CloseHandle(pi.hProcess);}
            Sleep(30000);
        }
        return 0;
    }
    if(argc!=2)return 2;
    for(const auto& argument:{std::wstring(),std::wstring(L"with spaces"),std::wstring(L"a\"b\\"),std::wstring(L"C:\\folder with spaces\\")}){
        auto command=L"program "+jd::offline::Quote(argument);int count=0;auto args=CommandLineToArgvW(command.c_str(),&count);
        Check(args&&count==2&&argument==args[1],"Windows argument round trip failed");LocalFree(args);
    }
    const std::filesystem::path root(argv[1]),data=root/L"данные с пробелом";
    std::filesystem::create_directories(root/L"runtime/python");std::filesystem::create_directories(data);
    wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);
    std::filesystem::copy_file(self,root/L"runtime/python/python.exe",std::filesystem::copy_options::overwrite_existing);
    Check(jd::offline::Run(root,L"game",data,L"home with spaces",[]{return false;}),"Offline child did not complete");
    Check(Pid(data/L"child.pid")!=0,"Quoted data directory was not received by child");
    std::filesystem::remove(data/L"child.pid");std::filesystem::remove(data/L"grandchild.pid");
    const auto start=GetTickCount64();
    Check(!jd::offline::Run(root,L"cancel",data,L"home",[&]{return (std::filesystem::exists(data/L"grandchild.pid")&&GetTickCount64()-start>250)||GetTickCount64()-start>5000;}),"Cancelled preparation succeeded");
    Check(GetTickCount64()-start<6500,"Cancellation stalled");
    Check(Pid(data/L"grandchild.pid")&&Exited(Pid(data/L"child.pid"))&&Exited(Pid(data/L"grandchild.pid")),"Preparation left descendant processes running");
    std::cout<<"Offline launch, Unicode paths and descendant cancellation passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
