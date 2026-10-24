#include "radio_tools.h"
#include "radio_stream.h"
#include "application.h"
#include "mcp_server.h"

void RegisterRadioTools() {
    auto& server = McpServer::GetInstance();
    server.AddTool("self.radio.play",
        "Play a direct AAC/ADTS or MP3 radio stream. First find the exact active station in "
        "knowledge base Radios. Pass its name and full URL unchanged. Never invent URLs. "
        "Returns accepted/queued, not confirmation of audible playback; conversation may close.",
        PropertyList({Property("name", kPropertyTypeString), Property("url", kPropertyTypeString)}),
        [](const PropertyList& properties) -> ReturnValue {
            auto name = properties["name"].value<std::string>();
            auto url = properties["url"].value<std::string>();
            if (!radio::ValidUrl(url) || !Application::GetInstance().QueueRadio(name, url)) {
                return std::string("{\"accepted\":false,\"error\":\"Invalid station or HTTP(S) URL\"}");
            }
            return std::string("{\"accepted\":true,\"state\":\"queued\"}");
        });
    server.AddTool("self.radio.stop", "Stop radio playback or a pending connection.", PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            Application::GetInstance().StopRadio();
            return std::string("{\"accepted\":true}");
        });
    server.AddTool("self.radio.status", "Get radio state, name and any playback error.", PropertyList(),
        [](const PropertyList&) -> ReturnValue { return Application::GetInstance().RadioStatus(); });
}
