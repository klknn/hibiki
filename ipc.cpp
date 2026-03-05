#include "ipc.hpp"
#include "vst3_host.hpp"
#include <iostream>
#include <mutex>
#include "hibiki_request_generated.h"
#include "hibiki_response_generated.h"

namespace hibiki {

void sendNotification(const uint8_t* buf, size_t size) {
    static std::mutex cout_mutex;
    std::lock_guard<std::mutex> lock(cout_mutex);
    uint32_t msg_size = static_cast<uint32_t>(size);
    std::cout.write(reinterpret_cast<const char*>(&msg_size), sizeof(msg_size));
    std::cout.write(reinterpret_cast<const char*>(buf), size);
    std::cout.flush();
}

void sendAck(const char* cmd_type, bool success) {
    flatbuffers::FlatBufferBuilder builder(128);
    auto cmd_type_off = builder.CreateString(cmd_type);
    auto ack_off = hibiki::ipc::CreateAcknowledge(builder, cmd_type_off, success);
    auto nf_off = hibiki::ipc::CreateNotification(builder, hibiki::ipc::Response_Acknowledge, ack_off.Union());
    builder.Finish(nf_off);
    sendNotification(builder.GetBufferPointer(), builder.GetSize());
}

void sendParamList(int track_idx, int plugin_idx, const std::string& plugin_name, bool is_instrument, const std::vector<VstParamInfo>& params) {
    flatbuffers::FlatBufferBuilder builder(1024);
    std::vector<flatbuffers::Offset<hibiki::ipc::ParamInfo>> param_offsets;
    for (const auto& p : params) {
        auto name_off = builder.CreateString(p.name.c_str());
        param_offsets.push_back(hibiki::ipc::CreateParamInfo(builder, p.id, name_off, p.defaultValue));
    }
    auto params_vec = builder.CreateVector(param_offsets);
    auto name_off = builder.CreateString(plugin_name.c_str());
    auto list_off = hibiki::ipc::CreateParamList(builder, track_idx, plugin_idx, name_off, is_instrument, params_vec);
    auto nf_off = hibiki::ipc::CreateNotification(builder, hibiki::ipc::Response_ParamList, list_off.Union());
    builder.Finish(nf_off);
    sendNotification(builder.GetBufferPointer(), builder.GetSize());
}

void sendLog(const std::string& msg) {
    flatbuffers::FlatBufferBuilder builder(512);
    auto msg_off = builder.CreateString(msg.c_str());
    auto log_off = hibiki::ipc::CreateLog(builder, msg_off);
    auto nf_off = hibiki::ipc::CreateNotification(builder, hibiki::ipc::Response_Log, log_off.Union());
    builder.Finish(nf_off);
    sendNotification(builder.GetBufferPointer(), builder.GetSize());
}

void sendClipInfo(int track_idx, int slot_index, const std::string& name, const std::string& path) {
    flatbuffers::FlatBufferBuilder builder(512);
    auto name_off = builder.CreateString(name.c_str());
    auto path_off = builder.CreateString(path.c_str());
    auto clip_off = hibiki::ipc::CreateClipInfo(builder, track_idx, slot_index, name_off, path_off);
    auto nf_off = hibiki::ipc::CreateNotification(builder, hibiki::ipc::Response_ClipInfo, clip_off.Union());
    builder.Finish(nf_off);
    sendNotification(builder.GetBufferPointer(), builder.GetSize());
}

void sendClearProject() {
    flatbuffers::FlatBufferBuilder builder(128);
    auto clear_off = hibiki::ipc::CreateClearProject(builder);
    auto nf_off = hibiki::ipc::CreateNotification(builder, hibiki::ipc::Response_ClearProject, clear_off.Union());
    builder.Finish(nf_off);
    sendNotification(builder.GetBufferPointer(), builder.GetSize());
}

} // namespace hibiki
