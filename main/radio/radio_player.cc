#include "radio_player.h"
#include "radio_stream.h"
#include "audio_service.h"
#include "board.h"
#include "esp_audio_simple_dec.h"
#include "esp_aac_dec.h"
#include "esp_mp3_dec.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_timer.h>
#include <cJSON.h>

namespace {
struct StreamResources {
    esp_http_client_handle_t http = nullptr;
    esp_audio_simple_dec_handle_t decoder = nullptr;
    esp_ae_rate_cvt_handle_t resampler = nullptr;
    std::string content_type;
    size_t icy_interval = 0;
    ~StreamResources() {
        if (http) esp_http_client_cleanup(http);
        if (decoder) esp_audio_simple_dec_close(decoder);
        if (resampler) esp_ae_rate_cvt_close(resampler);
    }
};

esp_err_t HeaderEvent(esp_http_client_event_t* event) {
    auto* stream = static_cast<StreamResources*>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value) {
        if (strcasecmp(event->header_key, "Content-Type") == 0) {
            stream->content_type.assign(event->header_value, strnlen(event->header_value, 128));
            std::transform(stream->content_type.begin(), stream->content_type.end(),
                           stream->content_type.begin(), [](unsigned char c) { return std::tolower(c); });
        } else if (strcasecmp(event->header_key, "icy-metaint") == 0) {
            char* end = nullptr;
            auto interval = strtoul(event->header_value, &end, 10);
            if (end != event->header_value && *end == '\0' && interval <= 16 * 1024 * 1024) {
                stream->icy_interval = interval;
            }
        }
    }
    return ESP_OK;
}
}  // namespace

bool RadioPlayer::Play(const std::string& name, const std::string& url) {
    if (name.empty() || name.size() > 160 || !radio::ValidUrl(url)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!task_ && xTaskCreate([](void* arg) { static_cast<RadioPlayer*>(arg)->Worker(); },
                              "radio_stream", 8192, this, 3, &task_) != pdPASS) {
        state_ = "error";
        error_ = "Nao foi possivel criar o reprodutor";
        return false;
    }
    if (audio_token_) audio_.EndExternalPlayback(audio_token_);
    audio_token_ = audio_.BeginExternalPlayback();
    name_ = name;
    url_ = url;
    error_.clear();
    state_ = "connecting";
    ++generation_;
    xTaskNotifyGive(task_);
    return true;
}

void RadioPlayer::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++generation_;
    if (audio_token_) audio_.EndExternalPlayback(audio_token_);
    audio_token_ = 0;
    state_ = "stopped";
    error_.clear();
    if (task_) xTaskNotifyGive(task_);
}

bool RadioPlayer::IsActive() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == "connecting" || state_ == "playing";
}

std::string RadioPlayer::Status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "state", state_.c_str());
    cJSON_AddStringToObject(json, "name", name_.c_str());
    cJSON_AddStringToObject(json, "error", error_.c_str());
    char* text = cJSON_PrintUnformatted(json);
    std::string result = text ? text : "{}";
    cJSON_free(text);
    cJSON_Delete(json);
    return result;
}

void RadioPlayer::Worker() {
    const bool codecs_ready = esp_aac_dec_register() == ESP_AUDIO_ERR_OK &&
                              esp_mp3_dec_register() == ESP_AUDIO_ERR_OK;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint32_t generation, token;
        std::string url;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (state_ != "connecting") continue;
            generation = generation_.load();
            token = audio_token_;
            url = url_;
        }
        auto error = codecs_ready ? Stream(generation, token, url) : "Falha ao registrar codecs";
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation == generation_.load()) {
            audio_.EndExternalPlayback(token);
            audio_token_ = 0;
            state_ = "error";
            error_ = error.empty() ? "Transmissao encerrada" : error;
        }
    }
}

std::string RadioPlayer::Stream(uint32_t generation, uint32_t token, const std::string& url) {
    auto cancelled = [this, generation]() { return generation_.load() != generation; };
    StreamResources stream;
    esp_http_client_config_t http_cfg = {};
    http_cfg.url = url.c_str();
    http_cfg.timeout_ms = 5000;
    http_cfg.buffer_size = 2048;
    http_cfg.buffer_size_tx = 3072;
    http_cfg.disable_auto_redirect = true;
    http_cfg.crt_bundle_attach = esp_crt_bundle_attach;
    http_cfg.event_handler = HeaderEvent;
    http_cfg.user_data = &stream;
    stream.http = esp_http_client_init(&http_cfg);
    if (!stream.http) return "Sem memoria para HTTP";
    esp_http_client_set_header(stream.http, "User-Agent", "Xiaozhi-Radio/1.0");
    esp_http_client_set_header(stream.http, "Icy-MetaData", "0");
    bool opened = false;
    for (int redirect = 0; redirect <= 5 && !cancelled(); ++redirect) {
        stream.content_type.clear();
        stream.icy_interval = 0;
        if (esp_http_client_open(stream.http, 0) != ESP_OK) return "Falha de conexao ou TLS";
        if (esp_http_client_fetch_headers(stream.http) < 0) return "Falha nos cabecalhos HTTP";
        int status = esp_http_client_get_status_code(stream.http);
        if (status == 200) { opened = true; break; }
        if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308) {
            return "HTTP " + std::to_string(status);
        }
        if (redirect == 5 || esp_http_client_set_redirection(stream.http) != ESP_OK) {
            return "Redirecionamentos invalidos ou excessivos";
        }
        esp_http_client_close(stream.http);
    }
    if (cancelled()) return {};
    if (!opened) return "Nao foi possivel abrir a transmissao";
    const bool aac = stream.content_type.find("audio/aac") == 0;
    const bool mp3 = stream.content_type.find("audio/mpeg") == 0 ||
                     stream.content_type.find("audio/mp3") == 0;
    if (!aac && !mp3) return "Formato nao suportado: use stream direto AAC/ADTS ou MP3";

    esp_aac_dec_cfg_t aac_cfg = ESP_AAC_DEC_CONFIG_DEFAULT();
    aac_cfg.aac_plus_enable = true;
    esp_audio_simple_dec_cfg_t dec_cfg = {};
    dec_cfg.dec_type = aac ? ESP_AUDIO_SIMPLE_DEC_TYPE_AAC : ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    dec_cfg.dec_cfg = aac ? &aac_cfg : nullptr;
    dec_cfg.cfg_size = aac ? sizeof(aac_cfg) : 0;
    if (esp_audio_simple_dec_open(&dec_cfg, &stream.decoder) != ESP_AUDIO_ERR_OK) {
        return "Falha ao abrir decodificador";
    }
    radio::IcyFilter icy(stream.icy_interval);
    std::array<uint8_t, 2048> bytes;
    std::vector<uint8_t> encoded;
    encoded.reserve(bytes.size());
    std::vector<int16_t> pcm(8192), mono, resampled;
    mono.reserve(4096);
    resampled.reserve(8192);
    uint32_t input_rate = 0;
    const uint32_t output_rate = Board::GetInstance().GetAudioCodec()->output_sample_rate();
    auto last_pcm = esp_timer_get_time();
    while (!cancelled()) {
        int count = esp_http_client_read(stream.http, reinterpret_cast<char*>(bytes.data()), bytes.size());
        if (count <= 0) return "Transmissao encerrada ou tempo de leitura esgotado";
        icy.Feed(bytes.data(), count, encoded);
        size_t offset = 0;
        while (offset < encoded.size() && !cancelled()) {
            esp_audio_simple_dec_raw_t raw = {};
            raw.buffer = encoded.data() + offset;
            raw.len = encoded.size() - offset;
            esp_audio_simple_dec_out_t out = {};
            out.buffer = reinterpret_cast<uint8_t*>(pcm.data());
            out.len = pcm.size() * sizeof(int16_t);
            auto result = esp_audio_simple_dec_process(stream.decoder, &raw, &out);
            if (result != ESP_AUDIO_ERR_OK || raw.consumed > raw.len) return "Erro ao decodificar audio";
            offset += raw.consumed;
            if (!raw.consumed && !out.decoded_size) return "Decodificador sem progresso";
            if (!out.decoded_size) continue;
            esp_audio_simple_dec_info_t info = {};
            if (esp_audio_simple_dec_get_info(stream.decoder, &info) != ESP_AUDIO_ERR_OK ||
                info.bits_per_sample != 16 || info.channel < 1 || info.channel > 2 ||
                info.sample_rate < 8000 || info.sample_rate > 48000 || out.decoded_size > out.len) {
                return "Formato PCM nao suportado";
            }
            size_t samples = out.decoded_size / sizeof(int16_t) / info.channel;
            mono.resize(samples);
            for (size_t i = 0; i < samples; ++i) {
                mono[i] = info.channel == 1 ? pcm[i] :
                    static_cast<int16_t>((static_cast<int32_t>(pcm[2*i]) + pcm[2*i+1]) / 2);
            }
            if (input_rate != info.sample_rate) {
                if (stream.resampler) esp_ae_rate_cvt_close(stream.resampler);
                stream.resampler = nullptr;
                input_rate = info.sample_rate;
                if (input_rate != output_rate) {
                    esp_ae_rate_cvt_cfg_t rate_cfg = {};
                    rate_cfg.src_rate = input_rate;
                    rate_cfg.dest_rate = output_rate;
                    rate_cfg.channel = 1;
                    rate_cfg.bits_per_sample = 16;
                    rate_cfg.complexity = 2;
                    rate_cfg.perf_type = ESP_AE_RATE_CVT_PERF_TYPE_SPEED;
                    if (esp_ae_rate_cvt_open(&rate_cfg, &stream.resampler) != ESP_AE_ERR_OK) {
                        return "Falha na conversao de frequencia";
                    }
                }
            }
            auto* output = &mono;
            if (stream.resampler) {
                uint32_t capacity = 0;
                esp_ae_rate_cvt_get_max_out_sample_num(stream.resampler, mono.size(), &capacity);
                if (capacity > 16384) return "Frame de audio excessivo";
                resampled.resize(capacity);
                if (esp_ae_rate_cvt_process(stream.resampler, mono.data(), mono.size(),
                                           resampled.data(), &capacity) != ESP_AE_ERR_OK) {
                    return "Erro na conversao de frequencia";
                }
                resampled.resize(capacity);
                output = &resampled;
            }
            // Twenty millisecond chunks bound the residual audio when the user interrupts.
            for (size_t i = 0; i < output->size() && !cancelled(); i += output_rate / 50) {
                size_t length = std::min<size_t>(output_rate / 50, output->size() - i);
                if (!audio_.PushExternalPcm(token, output->data() + i, length)) return "Saida de audio interrompida";
                std::lock_guard<std::mutex> lock(mutex_);
                if (generation == generation_.load()) state_ = "playing";
            }
            last_pcm = esp_timer_get_time();
        }
        if (esp_timer_get_time() - last_pcm > 10000000) return "Nenhum audio valido recebido";
    }
    return {};
}
