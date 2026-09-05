// Deterministic reproduction of asynchronous close retaining a mux ICE agent.
// The stalled teardown worker models the scheduling race observed on CI.
#include "rtc/rtc.h"
#include "juice/juice.h"
#include "impl/processor.hpp"
#include "impl/peerconnection.hpp"
#include <cstdlib>
#include <future>
#include <iostream>
using namespace std::chrono_literals;
static constexpr int port = 49195;
static void require(bool condition, const char *message) {
    if (!condition) { std::cerr << message << std::endl; std::abort(); }
}
static bool reject(const void *, size_t, const char *, uint16_t, void *) { return false; }
static int agents() {
    juice_mux_stats_t stats{};
    require(juice_mux_get_stats("127.0.0.1", port, &stats) == 0, "read mux stats");
    return stats.agents;
}
static std::pair<int,int> peer(const char *ufrag) {
    rtcConfiguration config{};
    config.bindAddress = "127.0.0.1";
    config.enableIceUdpMux = true;
    config.disableAutoNegotiation = true;
    config.portRangeBegin = port; config.portRangeEnd = port;
    int pc = rtcCreatePeerConnection(&config); require(pc >= 0, "create peer");
    int dc = rtcCreateDataChannel(pc, "test"); require(dc >= 0, "create channel");
    rtcLocalDescriptionInit init{ufrag, "publicTestPassword0000000000"};
    require(rtcSetLocalDescriptionEx(pc, "offer", &init) == 0, "create ICE agent");
    require(agents() == 1, "one agent before close");
    return {pc,dc};
}
struct BlockTeardown {
    std::promise<void> entered, release;
    BlockTeardown() {
        auto future = release.get_future().share();
        rtc::impl::TearDownProcessor::Instance().enqueue([this, future] { entered.set_value(); future.wait(); });
        require(entered.get_future().wait_for(5s) == std::future_status::ready, "teardown worker entered barrier");
    }
    ~BlockTeardown() { release.set_value(); rtc::impl::TearDownProcessor::Instance().join(); }
};
int main() {
    require(juice_mux_listen_raw("127.0.0.1", port, reject, nullptr) == 0, "own endpoint");
    {
        auto [pc, dc] = peer("oldCloseUfrag");
        BlockTeardown blocked;
        require(rtcDeleteDataChannel(dc) == 0, "delete old channel");
        require(rtcDeletePeerConnection(pc) == 0, "legacy delete returned");
        require(agents() == 1, "reproduce: legacy delete returns with a native agent still alive");
        std::cout << "reproduction PASS legacyDeleteReturned=true remainingIceAgents=1" << std::endl;
    }
    require(agents() == 0, "legacy teardown eventually releases agent");
    auto [pc, dc] = peer("awaitCloseUfrag");
    {
        BlockTeardown blocked;
        require(rtcClosePeerConnectionAndWait(pc, 20) == RTC_ERR_NOT_AVAIL, "bounded wait reports timeout, not cleanup success");
        require(agents() == 1, "capacity must remain reserved during timeout");
    }
    require(rtcClosePeerConnectionAndWait(pc, 5000) == 0, "await exact peer teardown");
    require(agents() == 0, "wait succeeds only after agent release");
    require(rtcDeleteDataChannel(dc) == 0, "delete channel");
    require(rtcDeletePeerConnection(pc) == 0, "delete awaited peer");
    {
        rtc::Configuration config;
        config.bindAddress = "127.0.0.1"; config.enableIceUdpMux = true;
        config.portRangeBegin = port; config.portRangeEnd = port;
        auto retainedPeer = std::make_shared<rtc::impl::PeerConnection>(config);
        auto retainedIce = retainedPeer->initIceTransport();
        retainedIce->gatherLocalCandidates("0");
        require(agents() == 1, "held ICE reference owns agent");
        retainedPeer->remoteClose();
        rtc::impl::TearDownProcessor::Instance().join();
        require(agents() == 1, "teardown task finished but external transport reference remains");
        require(!retainedPeer->closeAndWait(20ms), "completion must not precede final ICE transport destruction");
        retainedIce.reset();
        require(retainedPeer->closeAndWait(5s), "last transport destruction completes teardown");
        require(agents() == 0, "no agent after last reference release");
        std::cout << "retained-reference PASS waitTimesOutUntilFinalDestruction=true" << std::endl;
    }
    require(juice_mux_listen_raw("127.0.0.1", port, nullptr, nullptr) == 0, "release endpoint");
    std::cout << "remedy PASS completedTeardown=true remainingIceAgents=0" << std::endl;
}
