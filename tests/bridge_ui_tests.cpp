#include <afxwin.h>
#include "../src/main_frame.h"
#include "../src/resource.h"
#include "synthetic_bridge.h"
#include <iostream>

CWinApp test_app;
namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Pump() {
    MSG msg{};
    while (PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
        if (msg.message != WM_QUIT) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
}
template<class F> void Until(F condition) {
    const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(4);
    while (!condition() && std::chrono::steady_clock::now()<deadline) { Pump(); Sleep(1); }
    Check(condition(),"UI condition timeout");
}
std::wstring Text(HWND control) {
    wchar_t text[256]{}; GetWindowTextW(control,text,256); return text;
}
HWND Find(HWND parent, const wchar_t* caption) {
    struct Search { const wchar_t* caption; HWND result=nullptr; } search{caption};
    EnumChildWindows(parent,[](HWND w,LPARAM p)->BOOL {
        auto& s=*reinterpret_cast<Search*>(p);
        if (Text(w)==s.caption) { s.result=w; return FALSE; } return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    Check(search.result!=nullptr,"missing control"); return search.result;
}
void Click(HWND control) {
    SendMessageW(GetParent(control),WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(control),BN_CLICKED),reinterpret_cast<LPARAM>(control));
}
}

int wmain() {
    try {
        Check(AfxWinInit(GetModuleHandleW(nullptr),nullptr,GetCommandLineW(),0)!=FALSE,"MFC init");
        auto fake=std::make_unique<SyntheticBridge>(); auto* io=fake.get();
        auto* frame=new CMainFrame(std::move(fake));
        Check(frame->Create(nullptr,L"Synthetic SGPIO contract",WS_OVERLAPPEDWINDOW,CRect(0,0,1280,760))!=FALSE,"frame create");
        const HWND window=frame->GetSafeHwnd();
        const HWND chip=GetDlgItem(window,ID_TOP_STATUS_VALUE);
        Until([&] { return Text(chip)==L"Disconnected"; });
        const HWND connect=GetDlgItem(window,ID_TOP_CONNECT_BTN);
        const HWND disconnect=GetDlgItem(window,ID_TOP_DISCONNECT_BTN);
        Click(connect);
        Check(Text(chip)==L"Busy" && IsWindowEnabled(disconnect) && !IsWindowEnabled(connect),"connect busy guards");
        Check(!IsWindowEnabled(GetDlgItem(window,ID_TOP_PING_BTN)),"ping enabled during connect");
        Until([&] { return Text(chip)==L"Connected"; });
        const HWND output=GetDlgItem(GetDlgItem(window,ID_TAB_CTRL),ID_TAB_SGPIO);
        const HWND monitor=GetDlgItem(GetDlgItem(window,ID_TAB_CTRL),ID_TAB_SGPIO_MONITOR);
        Check(output && monitor,"tab windows");
        Check(IsWindowEnabled(Find(output,L"Enable")) && IsWindowEnabled(Find(monitor,L"Arm")),"idle owners did not enable both tabs together");
        const int calls_before_confirmation=io->calls;
        std::atomic<bool> modal_busy{false};
        const DWORD ui_thread=GetCurrentThreadId();
        std::thread cancel_confirmation([&] {
            const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            HWND dialog=nullptr;
            while (!dialog && std::chrono::steady_clock::now()<limit) {
                struct Search { HWND owner; HWND found=nullptr; } search{window};
                EnumThreadWindows(ui_thread,[](HWND w,LPARAM p)->BOOL {
                    auto& item=*reinterpret_cast<Search*>(p);
                    if (GetWindow(w,GW_OWNER)==item.owner && Text(w)==L"Return Bootloader") {item.found=w; return FALSE;}
                    return TRUE;
                },reinterpret_cast<LPARAM>(&search));
                dialog=search.found;
                if (!dialog) Sleep(1);
            }
            if (dialog) {
                modal_busy=Text(chip)==L"Busy" && !IsWindowEnabled(GetDlgItem(window,ID_TOP_PING_BTN));
                PostMessageW(dialog,WM_COMMAND,IDNO,0);
            }
        });
        Click(GetDlgItem(window,ID_TOP_RETURN_BOOTLOADER_BTN));
        cancel_confirmation.join();
        Check(modal_busy && io->calls==calls_before_confirmation && Text(chip)==L"Connected","confirmation cancel/busy emitted I/O");
        Click(Find(output,L"Enable"));
        Check(Text(chip)==L"Busy" && !IsWindowEnabled(Find(monitor,L"Arm")),"cross-tab busy");
        Click(GetDlgItem(window,ID_TOP_PING_BTN)); // Even a forged disabled-button command is guarded.
        Until([&] { return Text(chip)==L"Connected"; });
        Check(io->output && !IsWindowEnabled(Find(monitor,L"Arm")),"output ownership");
        Click(Find(output,L"Disable")); Until([&] { return Text(chip)==L"Connected"; });
        Check(!io->output && IsWindowEnabled(Find(monitor,L"Arm")),"output release after readback");
        Click(Find(monitor,L"Arm")); Until([&] { return Text(chip)==L"Connected"; });
        Check(io->monitor && !IsWindowEnabled(Find(output,L"Enable")),"monitor ownership");
        io->fail_poll=true;
        Until([&] { return Text(chip)==L"Disconnected"; });
        Check(!IsWindowEnabled(Find(output,L"Enable")),"poll failure allowed output");
        Check(io->monitor,"local failure fabricated remote stop");
        io->fail_poll=false; io->monitor=false; io->hold=true;
        Click(connect); Until([&] { return io->entered.load(); }); Click(disconnect);
        Check(Text(chip)==L"Cancelling" && !IsWindowEnabled(connect),"cancel pending not busy");
        Until([&] { return Text(chip)==L"Disconnected"; });
        Check(!io->opened && io->closes>=2,"late successful connect handle retained");
        io->hold=true; Click(connect); Until([&] { return io->entered.load(); });
        SendMessageW(window,WM_CLOSE,0,0);
        Until([&] { return !IsWindow(window); });
        std::cout << "PASS MFC actual WM_COMMAND/timer: connect, cross-tab busy, confirmation No/no-I/O, disabled-handler guard, ownership, poll loss, late-open cancel, asynchronous close\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n'; return 1; }
}
