#include "media_service.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cmath>
#include "crow/logging.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>
}

// Define implementations for dr_libs (only once in one cpp file)
#define DR_WAV_IMPLEMENTATION
#include "../../third_party/dr_libs/dr_wav.h"
#define DR_MP3_IMPLEMENTATION
#include "../../third_party/dr_libs/dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#include "../../third_party/dr_libs/dr_flac.h"

namespace services {

// RAII helper for dr_libs decoders
struct AudioDecoder {
    enum Type { NONE, WAV, MP3, FLAC };
    Type type = NONE;
    drwav wav;
    drmp3 mp3;
    drflac* flac = nullptr;

    ~AudioDecoder() {
        if (type == WAV) drwav_uninit(&wav);
        else if (type == MP3) drmp3_uninit(&mp3);
        else if (type == FLAC && flac) drflac_close(flac);
    }

    size_t read_f32(float* buffer, size_t frames) {
        if (type == WAV) return drwav_read_pcm_frames_f32(&wav, frames, buffer);
        if (type == MP3) return drmp3_read_pcm_frames_f32(&mp3, frames, buffer);
        if (type == FLAC) return drflac_read_pcm_frames_f32(flac, frames, buffer);
        return 0;
    }
};

// Helper to log FFmpeg errors
void log_av_error(int err, const std::string& msg) {
    char errbuf[256];
    av_strerror(err, errbuf, sizeof(errbuf));
    CROW_LOG_ERROR << msg << ": " << errbuf;
}

bool MediaService::transcode_audio(const std::string& input_filepath, const std::string& output_filepath, const std::string& target_format) {
    AVFormatContext* ifmt_ctx = nullptr;
    AVFormatContext* ofmt_ctx = nullptr;
    AVCodecContext* dec_ctx = nullptr;
    AVCodecContext* enc_ctx = nullptr;
    SwrContext* resample_ctx = nullptr;
    AVAudioFifo* fifo = nullptr;
    int audio_stream_index = -1;
    bool success = false;
    int64_t pts = 0;

    CROW_LOG_INFO << "Transcoding " << input_filepath << " to " << target_format << " (" << output_filepath << ")";

    // 1. Open Input
    if (avformat_open_input(&ifmt_ctx, input_filepath.c_str(), nullptr, nullptr) < 0) {
        CROW_LOG_ERROR << "Could not open input file: " << input_filepath;
        return false;
    }

    if (avformat_find_stream_info(ifmt_ctx, nullptr) < 0) {
        CROW_LOG_ERROR << "Could not find stream information";
        goto cleanup;
    }

    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
        if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            break;
        }
    }

    if (audio_stream_index == -1) {
        CROW_LOG_ERROR << "Could not find audio stream in input";
        goto cleanup;
    }

    // 2. Setup Decoder
    {
        const AVCodec* decoder = avcodec_find_decoder(ifmt_ctx->streams[audio_stream_index]->codecpar->codec_id);
        if (!decoder) {
            CROW_LOG_ERROR << "Failed to find decoder";
            goto cleanup;
        }

        dec_ctx = avcodec_alloc_context3(decoder);
        avcodec_parameters_to_context(dec_ctx, ifmt_ctx->streams[audio_stream_index]->codecpar);
        if (avcodec_open2(dec_ctx, decoder, nullptr) < 0) {
            CROW_LOG_ERROR << "Failed to open decoder";
            goto cleanup;
        }
    }

    // 3. Setup Output & Encoder
    {
        avformat_alloc_output_context2(&ofmt_ctx, nullptr, nullptr, output_filepath.c_str());
        if (!ofmt_ctx) {
            CROW_LOG_ERROR << "Could not create output context";
            goto cleanup;
        }

        AVCodecID out_codec_id = ofmt_ctx->oformat->audio_codec;
        if (target_format == "mp3") out_codec_id = AV_CODEC_ID_MP3;
        else if (target_format == "ogg") out_codec_id = AV_CODEC_ID_VORBIS;
        else if (target_format == "wav") out_codec_id = AV_CODEC_ID_PCM_S16LE;

        const AVCodec* encoder = avcodec_find_encoder(out_codec_id);
        if (!encoder) {
            CROW_LOG_ERROR << "Failed to find encoder for " << target_format;
            goto cleanup;
        }

        enc_ctx = avcodec_alloc_context3(encoder);
        enc_ctx->sample_rate = dec_ctx->sample_rate;
        av_channel_layout_copy(&enc_ctx->ch_layout, &dec_ctx->ch_layout);
        if (enc_ctx->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) {
            av_channel_layout_default(&enc_ctx->ch_layout, dec_ctx->ch_layout.nb_channels);
        }
        enc_ctx->sample_fmt = encoder->sample_fmts[0];
        enc_ctx->time_base = {1, enc_ctx->sample_rate};

        if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
            enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        if (avcodec_open2(enc_ctx, encoder, nullptr) < 0) {
            CROW_LOG_ERROR << "Failed to open encoder";
            goto cleanup;
        }

        AVStream* out_stream = avformat_new_stream(ofmt_ctx, nullptr);
        avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
        out_stream->time_base = enc_ctx->time_base;
    }

    // 4. Setup Resampler and FIFO
    {
        if (swr_alloc_set_opts2(&resample_ctx,
                &enc_ctx->ch_layout, enc_ctx->sample_fmt, enc_ctx->sample_rate,
                &dec_ctx->ch_layout, dec_ctx->sample_fmt, dec_ctx->sample_rate,
                0, nullptr) < 0) {
            CROW_LOG_ERROR << "Failed to allocate resampler context";
            goto cleanup;
        }
        if (swr_init(resample_ctx) < 0) {
            CROW_LOG_ERROR << "Failed to initialize resampler";
            goto cleanup;
        }

        fifo = av_audio_fifo_alloc(enc_ctx->sample_fmt, enc_ctx->ch_layout.nb_channels, 1);
        if (!fifo) {
            CROW_LOG_ERROR << "Failed to allocate FIFO";
            goto cleanup;
        }
    }

    // 5. Open output file
    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&ofmt_ctx->pb, output_filepath.c_str(), AVIO_FLAG_WRITE) < 0) {
            CROW_LOG_ERROR << "Could not open output file for writing";
            goto cleanup;
        }
    }

    if (avformat_write_header(ofmt_ctx, nullptr) < 0) {
        CROW_LOG_ERROR << "Error writing header";
        goto cleanup;
    }

    // 6. Transcoding Loop
    {
        AVPacket* packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        AVFrame* enc_frame = av_frame_alloc();
        AVPacket* out_packet = av_packet_alloc();
        int ret;

        auto encode_from_fifo = [&]() -> bool {
            int frame_size = enc_ctx->frame_size ? enc_ctx->frame_size : 1024;
            while (av_audio_fifo_size(fifo) >= frame_size || (success && av_audio_fifo_size(fifo) > 0)) {
                int current_frame_size = std::min(av_audio_fifo_size(fifo), frame_size);
                
                enc_frame->nb_samples = current_frame_size;
                enc_frame->format = enc_ctx->sample_fmt;
                av_channel_layout_copy(&enc_frame->ch_layout, &enc_ctx->ch_layout);
                enc_frame->sample_rate = enc_ctx->sample_rate;
                
                if (av_frame_get_buffer(enc_frame, 0) < 0) return false;
                if (av_audio_fifo_read(fifo, (void**)enc_frame->data, current_frame_size) < current_frame_size) return false;

                enc_frame->pts = pts;
                pts += current_frame_size;

                if (avcodec_send_frame(enc_ctx, enc_frame) >= 0) {
                    while (avcodec_receive_packet(enc_ctx, out_packet) >= 0) {
                        av_packet_rescale_ts(out_packet, enc_ctx->time_base, ofmt_ctx->streams[0]->time_base);
                        out_packet->stream_index = 0;
                        av_interleaved_write_frame(ofmt_ctx, out_packet);
                        av_packet_unref(out_packet);
                    }
                }
                av_frame_unref(enc_frame);
                if (success && av_audio_fifo_size(fifo) == 0) break;
            }
            return true;
        };

        while (av_read_frame(ifmt_ctx, packet) >= 0) {
            if (packet->stream_index == audio_stream_index) {
                ret = avcodec_send_packet(dec_ctx, packet);
                while (ret >= 0) {
                    ret = avcodec_receive_frame(dec_ctx, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                    else if (ret < 0) goto loop_cleanup;

                    // Resample
                    int max_dst_nb_samples = swr_get_out_samples(resample_ctx, frame->nb_samples);
                    uint8_t** dst_data = nullptr;
                    int dst_linesize;
                    if (av_samples_alloc_array_and_samples(&dst_data, &dst_linesize, enc_ctx->ch_layout.nb_channels, max_dst_nb_samples, enc_ctx->sample_fmt, 0) < 0) goto loop_cleanup;

                    int resampled_samples = swr_convert(resample_ctx, dst_data, max_dst_nb_samples, (const uint8_t**)frame->data, frame->nb_samples);
                    if (resampled_samples > 0) {
                        if (av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) + resampled_samples) < 0) {
                            av_freep(&dst_data[0]);
                            av_freep(&dst_data);
                            goto loop_cleanup;
                        }
                        av_audio_fifo_write(fifo, (void**)dst_data, resampled_samples);
                    }

                    av_freep(&dst_data[0]);
                    av_freep(&dst_data);
                    
                    if (!encode_from_fifo()) goto loop_cleanup;
                }
            }
            av_packet_unref(packet);
        }

        // Flush resampler
        {
            uint8_t** dst_data = nullptr;
            int dst_linesize;
            int max_dst_nb_samples = swr_get_out_samples(resample_ctx, 0);
            if (max_dst_nb_samples > 0 && av_samples_alloc_array_and_samples(&dst_data, &dst_linesize, enc_ctx->ch_layout.nb_channels, max_dst_nb_samples, enc_ctx->sample_fmt, 0) >= 0) {
                int resampled_samples = swr_convert(resample_ctx, dst_data, max_dst_nb_samples, nullptr, 0);
                if (resampled_samples > 0) {
                    if (av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) + resampled_samples) >= 0) {
                        av_audio_fifo_write(fifo, (void**)dst_data, resampled_samples);
                    }
                }
                av_freep(&dst_data[0]);
                av_freep(&dst_data);
            }
        }

        // Flush FIFO and Encoder
        success = true; // Mark as flushing
        if (!encode_from_fifo()) goto loop_cleanup;

        avcodec_send_frame(enc_ctx, nullptr);
        while (avcodec_receive_packet(enc_ctx, out_packet) >= 0) {
            av_packet_rescale_ts(out_packet, enc_ctx->time_base, ofmt_ctx->streams[0]->time_base);
            out_packet->stream_index = 0;
            av_interleaved_write_frame(ofmt_ctx, out_packet);
            av_packet_unref(out_packet);
        }

        av_write_trailer(ofmt_ctx);

    loop_cleanup:
        av_packet_free(&packet);
        av_frame_free(&frame);
        av_frame_free(&enc_frame);
        av_packet_free(&out_packet);
    }

cleanup:
    if (dec_ctx) avcodec_free_context(&dec_ctx);
    if (enc_ctx) avcodec_free_context(&enc_ctx);
    if (ifmt_ctx) avformat_close_input(&ifmt_ctx);
    if (ofmt_ctx) {
        if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) avio_closep(&ofmt_ctx->pb);
        avformat_free_context(ofmt_ctx);
    }
    if (resample_ctx) swr_free(&resample_ctx);
    if (fifo) av_audio_fifo_free(fifo);

    if (success) CROW_LOG_INFO << "Successfully transcoded to " << target_format;
    else CROW_LOG_ERROR << "Failed to transcode to " << target_format;

    return success;
}

MediaResult MediaService::extract_waveform(const std::string& filepath, int resolution) {
// ... (rest of extract_waveform)
    MediaResult res;
    if (resolution <= 0) {
        res.error_message = "Invalid resolution";
        return res;
    }

    CROW_LOG_INFO << "Extracting waveform for: " << filepath << " (Resolution: " << resolution << ")";
    
    std::error_code ec;
    if (!std::filesystem::exists(filepath, ec)) {
        res.error_message = "File not found";
        CROW_LOG_ERROR << "Waveform extraction failed: " << res.error_message << ": " << filepath;
        return res;
    }

    res.file_size = (long long)std::filesystem::file_size(filepath, ec);
    std::string ext = std::filesystem::path(filepath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    unsigned int channels = 0;
    unsigned int sampleRate = 0;
    drwav_uint64 totalFrameCount = 0;
    AudioDecoder decoder;

    // 1. Open Decoder
    bool opened = false;
    if (ext == ".wav") {
        if (drwav_init_file(&decoder.wav, filepath.c_str(), NULL)) {
            decoder.type = AudioDecoder::WAV;
            channels = decoder.wav.channels;
            sampleRate = decoder.wav.sampleRate;
            totalFrameCount = decoder.wav.totalPCMFrameCount;
            res.format = "WAV";
            opened = true;
        }
    } else if (ext == ".mp3") {
        if (drmp3_init_file(&decoder.mp3, filepath.c_str(), NULL)) {
            decoder.type = AudioDecoder::MP3;
            channels = decoder.mp3.channels;
            sampleRate = decoder.mp3.sampleRate;
            totalFrameCount = drmp3_get_pcm_frame_count(&decoder.mp3);
            res.format = "MP3";
            opened = true;
        }
    } else if (ext == ".flac") {
        decoder.flac = drflac_open_file(filepath.c_str(), NULL);
        if (decoder.flac) {
            decoder.type = AudioDecoder::FLAC;
            channels = decoder.flac->channels;
            sampleRate = decoder.flac->sampleRate;
            totalFrameCount = decoder.flac->totalPCMFrameCount;
            res.format = "FLAC";
            opened = true;
        }
    }

    if (!opened) {
        res.error_message = "Unsupported file format: " + ext;
        CROW_LOG_ERROR << "Waveform extraction failed: " << res.error_message << " (" << filepath << ")";
        return res;
    }

    if (totalFrameCount == 0 || sampleRate == 0) {
        res.error_message = "Empty audio file or invalid sample rate";
        CROW_LOG_ERROR << "Waveform extraction failed: " << res.error_message;
        return res;
    }

    res.duration_seconds = (float)totalFrameCount / (float)sampleRate;
    
    // 2. Process PCM Data
    const size_t CHUNK_SIZE = 8192;
    std::vector<float> chunkBuffer(CHUNK_SIZE * channels);
    
    struct FloatPeak { float min = 0.0f; float max = 0.0f; };
    std::vector<FloatPeak> tempPeaks(resolution);
    
    double samplesPerWindow = (double)totalFrameCount / (double)resolution;
    float currentMin = 0.0f;
    float currentMax = 0.0f;
    double samplesInCurrentWindow = 0.0;
    int currentWindowIndex = 0;
    float globalMax = 0.0f;
    const float gamma = 0.5f;

    auto apply_gamma = [&](float x) {
        if (x == 0.0f) return 0.0f;
        return (x > 0.0f ? 1.0f : -1.0f) * std::pow(std::abs(x), gamma);
    };

    while (currentWindowIndex < resolution) {
        size_t framesRead = decoder.read_f32(chunkBuffer.data(), CHUNK_SIZE);
        if (framesRead == 0) break;

        for (size_t f = 0; f < framesRead; ++f) {
            float frameMin = 1.0f;
            float frameMax = -1.0f;
            for (unsigned int c = 0; c < channels; ++c) {
                float val = chunkBuffer[f * channels + c];
                if (val < frameMin) frameMin = val;
                if (val > frameMax) frameMax = val;
            }

            currentMin = std::min(currentMin, frameMin);
            currentMax = std::max(currentMax, frameMax);
            
            samplesInCurrentWindow += 1.0;

            if (samplesInCurrentWindow >= samplesPerWindow && currentWindowIndex < resolution) {
                float sMin = apply_gamma(currentMin);
                float sMax = apply_gamma(currentMax);

                tempPeaks[currentWindowIndex] = {sMin, sMax};
                globalMax = std::max({globalMax, std::abs(sMin), std::abs(sMax)});

                currentMin = 0.0f;
                currentMax = 0.0f;
                samplesInCurrentWindow -= samplesPerWindow;
                currentWindowIndex++;
            }
        }
    }

    // 3. Normalize and Quantize
    res.waveform_peaks.assign(resolution, {0, 0});
    if (globalMax > 1e-6f) {
        for (int i = 0; i < resolution; ++i) {
            res.waveform_peaks[i].minPeak = (int16_t)((tempPeaks[i].min / globalMax) * 32767.0f);
            res.waveform_peaks[i].maxPeak = (int16_t)((tempPeaks[i].max / globalMax) * 32767.0f);
        }
    }

    res.success = true;
    CROW_LOG_INFO << "Successfully extracted waveform for " << filepath << " (" << res.format << ")";
    return res;
}

} // namespace services
