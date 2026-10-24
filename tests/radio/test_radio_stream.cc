// Run with a C++20 host compiler, or include in an ESP-IDF Unity test app.
#include "../../main/radio/radio_stream.h"
#include <cassert>

int main() {
    radio::RequestEpoch requests;
    const auto queued = requests.Advance();
    requests.Advance();  // Wake/button cancellation arrives before scheduled request.
    assert(!requests.IsCurrent(queued));
    const auto stopped = requests.Advance();
    const auto replacement = requests.Advance();
    assert(!requests.IsCurrent(stopped));  // Stale stop must not cancel newer play.
    assert(requests.IsCurrent(replacement));
    assert(radio::ReleasePower(true, false, false, true));  // Stop, EOF or error in idle.
    assert(!radio::ReleasePower(true, false, false, false));  // Conversation owns performance.
    assert(!radio::ReleasePower(true, true, false, true));  // New station already active.
    assert(!radio::ReleasePower(true, false, true, true));  // New request pending.
    assert(!radio::ReleasePower(false, false, false, true));
    assert(radio::ValidUrl("https://11.stmip.net:8542/;"));
    assert(radio::ValidUrl("https://8189.brasilstream.com.br/stream?1792015041842"));
    assert(!radio::ValidUrl(""));
    assert(!radio::ValidUrl("file:///secret"));
    assert(!radio::ValidUrl("https:///no-host"));
    assert(!radio::ValidUrl("https://user:pass@example.com/a"));
    assert(!radio::ValidUrl("https://example.com/\r\nInjected: x"));
    assert(!radio::ValidUrl("https://example.com/" + std::string(2048, 'x')));
    std::vector<uint8_t> output, all;
    radio::IcyFilter filter(3);
    std::vector<uint8_t> wire = {'a', 'b', 'c', 1};
    wire.insert(wire.end(), 16, 'X');
    wire.insert(wire.end(), {'d', 'e', 'f', 0, 'g'});
    for (auto byte : wire) {
        filter.Feed(&byte, 1, output);
        all.insert(all.end(), output.begin(), output.end());
    }
    assert(std::string(all.begin(), all.end()) == "abcdefg");
}
