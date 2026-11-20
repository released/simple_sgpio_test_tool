#include "../src/core/bridge_service.h"
#include "../src/core/bridge_job.h"
#include "../src/hid/overlapped_io.h"
#include "synthetic_bridge.h"
#include <iostream>
#include <memory>

using namespace mfc_tool::core;
void Check(bool value, const char* name) { if (!value) throw std::runtime_error(name); }
template<class F> void Reject(F action, const char* name) {
    bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; }
    Check(rejected, name);
}
void WaitReady(BridgeJob& job) {
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!job.Ready() && std::chrono::steady_clock::now() < limit) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Check(job.Ready(), "job did not complete");
}

void ServiceTests() {
    mfc_tool::hid::DeviceInfo reports;
    reports.input_report_len=64; reports.output_report_len=64;
    Check(reports.HasBridgeReports(),"legacy 64-byte report layout rejected");
    reports.input_report_len=65; reports.output_report_len=65;
    Check(reports.HasBridgeReports(),"65-byte HID report-ID layout rejected");
    reports.output_report_len=63;
    Check(!reports.HasBridgeReports(),"short HID report layout admitted");
    auto fake = std::make_unique<SyntheticBridge>(); auto* io = fake.get(); BridgeService service(std::move(fake));
    Reject([&] { service.SgpioMonitorArm(); }, "unidentified board admitted Arm");
    service.Connect(0x416,0x5020,L"synthetic",100);
    Check(service.Model().board == BridgeBoard::M032 && service.Model().CanMonitor(), "M032 handshake");
    service.ApplyOutput(8,100000,true,100,0,0x1234,0x5678,0x9ABC);
    const std::vector<std::uint8_t> expected = {1,1,100,0,0,0x34,0x12,0x78,0x56,0xBC,0x9A};
    Check(io->last_apply == expected, "wire payload changed");
    const int calls = io->calls;
    Reject([&] { service.SgpioMonitorArm(); }, "active Output admitted Monitor");
    Check(io->calls == calls, "guard touched transport");
    service.SgpioOff(); service.SgpioMonitorConfig(16,3000); service.SgpioMonitorArm();
    Reject([&] { service.SgpioConfig(8,100000); }, "active Monitor admitted Output");
    io->fail_poll = true;
    Reject([&] { service.SgpioMonitorStatus(); }, "poll loss did not fail");
    Check(service.Model().monitor == PinState::Unknown && !service.Model().CanOutput() && !io->opened,
          "poll loss released unknown owner or retained handle");
    io->fail_poll = false; io->monitor = false; service.Connect(0x416,0x5020,L"synthetic",100);
    io->lose_apply_reply = true;
    Reject([&] { service.ApplyOutput(8,100000,true,100,0,1,0,0); }, "lost apply reply not raised");
    Check(io->output && !io->opened && service.Model().output == PinState::Unknown, "accepted/lost outcome mislabeled");
    const int after_loss = io->calls;
    Reject([&] { service.SgpioApply(true,true,100,0,1,0,0); }, "unsafe unknown apply replay");
    Check(io->calls == after_loss, "lost request auto replayed");
    io->lose_apply_reply = false; service.Connect(0x416,0x5020,L"synthetic",100);
    Check(service.Model().output == PinState::Active, "reconnect did not reconcile actual state");
    io->reject_apply = true;
    Reject([&] { service.SgpioApply(true,true,100,0,1,0,0); }, "bridge rejection ignored");
    Check(io->opened, "confirmed rejection needlessly disconnected");
    io->reject_apply = false; service.SgpioStatus(); service.SgpioOff();
    io->identity = "m261-sgpio-bridge/1.1.0";
    Reject([&] { service.GetInfo(); }, "mid-session board replacement accepted");
    service.Connect(0x416,0x5020,L"synthetic",100);
    Check(service.Model().board == BridgeBoard::M261, "M261 handshake");
    io->bad_pins = true;
    Reject([&] { service.Connect(0x416,0x5020,L"synthetic",100); }, "wrong pin board admitted");
    Check(!io->opened, "failed handshake handle leak");
    io->bad_pins = false; io->identity = "other-bridge/1.1.0";
    Reject([&] { service.Connect(0x416,0x5020,L"synthetic",100); }, "foreign board admitted");
    Check(!io->opened, "foreign board handle leak");
    io->identity = "m032-sgpio-bridge/1.0.0"; service.Connect(0x416,0x5020,L"synthetic",100);
    Check(service.Model().CanOutput() && !service.Model().CanMonitor(), "legacy output compatibility");
    SgpioModel model;
    for (size_t n=0; n<21; ++n) Reject([&] { model.OutputStatus(std::vector<std::uint8_t>(n,0)); }, "short Output frame admitted");
    for (size_t n=0; n<37; ++n) Reject([&] { model.MonitorStatus(std::vector<std::uint8_t>(n,0)); }, "short Monitor frame admitted");
    auto bad = std::vector<std::uint8_t>(37,0); bad[0]=9; bad[34]=6; bad[35]=4; bad[36]=5;
    Reject([&] { model.MonitorStatus(bad); }, "invalid monitor state admitted");
    auto good_monitor = std::vector<std::uint8_t>(37,0);
    good_monitor[12]=1; good_monitor[34]=6; good_monitor[35]=4; good_monitor[36]=5;
    for (size_t pin=34; pin<=36; ++pin) {
        auto wrong=good_monitor; wrong[pin]=7;
        Reject([&] { model.MonitorStatus(wrong); }, "one mismatched Monitor pin admitted");
    }
    auto huge=good_monitor; huge[6]=1; huge[7]=9;
    Reject([&] { model.MonitorStatus(huge); }, "excess sample count admitted");
    auto inconsistent=io->Transact(CMD_SGPIO_STATUS,{}); inconsistent[21]=1; inconsistent[22]=0; inconsistent[23]=1;
    Reject([&] { model.OutputStatus(inconsistent); }, "truncated SDIN capture admitted");
    io->identity="m261-sgpio-bridge/1.1.0"; service.Connect(0x416,0x5020,L"synthetic",100);
    service.SgpioMonitorArm(); service.SgpioMonitorStop();
    Check(service.Model().monitor==PinState::Idle && !io->monitor,"Monitor Stop/readback");
    std::cout << "PASS model/service identity, pin contracts, ownership, lost/rejected replies, reconciliation, payload compatibility\n";
}

void JobTests() {
    BridgeJob job; std::atomic<bool> release{false}; int applied = 0;
    Check(job.Start([&] { while (!release) std::this_thread::yield(); }, [&](auto) { ++applied; }), "start failed");
    Check(!job.Start([] {}, {}), "double start admitted");
    release = true; WaitReady(job);
    Check(job.Busy() && applied == 0, "completion-pending not busy");
    job.Consume(true);
    Check(!job.Busy() && applied == 0, "retired completion applied");
    Check(job.Start([] { throw std::runtime_error("fixture"); }, [&](auto error) { Check(error != nullptr,"error lost"); ++applied; }), "restart failed");
    WaitReady(job); job.Consume(); Check(applied == 1,"completion count");
    std::cout << "PASS job double-trigger, pending busy, discarded late result, exception, reuse\n";
}

void NativeIoTests() {
    const std::wstring name = L"\\\\.\\pipe\\sgpio-contract-" + std::to_wstring(GetCurrentProcessId());
    HANDLE server = CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,PIPE_TYPE_BYTE | PIPE_WAIT,1,4096,4096,0,nullptr);
    Check(server != INVALID_HANDLE_VALUE,"pipe server");
    HANDLE client = CreateFileW(name.c_str(),GENERIC_READ | GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
    Check(client != INVALID_HANDLE_VALUE,"pipe client");
    DWORD before=0,after=0; GetProcessHandleCount(GetCurrentProcess(),&before);
    for (int i=0;i<30;++i) {
        std::atomic<bool> cancel{false}; char buffer[16]={};
        std::thread canceller([&] { std::this_thread::sleep_for(std::chrono::milliseconds(2)); cancel=true; });
        Reject([&] { mfc_tool::hid::IoWithTimeout(client,buffer,sizeof(buffer),1000,cancel,false); }, "pending read cancellation");
        canceller.join();
    }
    std::atomic<bool> cancel{false}; char buffer[16]={};
    Reject([&] { mfc_tool::hid::IoWithTimeout(client,buffer,sizeof(buffer),25,cancel,false); },"read timeout");
    char value='X';
    Check(mfc_tool::hid::IoWithTimeout(server,&value,1,100,cancel,true)==1,"pipe write");
    Check(mfc_tool::hid::IoWithTimeout(client,buffer,sizeof(buffer),100,cancel,false)==1 && buffer[0]=='X',"post-cancel read reuse");
    GetProcessHandleCount(GetCurrentProcess(),&after);
    CloseHandle(client); CloseHandle(server);
    Check(after == before,"I/O event handle leak");
    std::cout << "PASS native overlapped pending cancel x30, timeout, event reclamation, post-cancel read/write\n";
}

int main() {
    try { ServiceTests(); JobTests(); NativeIoTests(); return 0; }
    catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
