#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "engine/core/history.hpp"
#include "engine/core/project.hpp"

namespace hibiki {

void playback_thread(ProjectState& state);
void listVst3Plugins(const char* path_ptr, size_t path_len);
void runVst3MainLoop();

ProjectState* createProjectState();
void destroyProjectState(ProjectState* state);

HistoryManager* createHistoryManager();
void destroyHistoryManager(HistoryManager* history);

bool isStateQuit(const ProjectState* state);
void setStateQuit(ProjectState* state, bool quit);

void setTimelinePlaying(ProjectState* state, bool playing);
bool isTimelinePlaying(const ProjectState* state);
bool isRecording(const ProjectState* state);
void setPlayheadPos(ProjectState* state, double pos);
double getPlayheadPos(const ProjectState* state);
void setBpm(ProjectState* state, double bpm);
double getBpm(const ProjectState* state);
void setLoop(ProjectState* state, bool enabled, double start_sec,
             double end_sec);
void stopPlaybackTracks(ProjectState* state);

void sendNotificationTick(ProjectState& state);
void sendInitialConfig(const ProjectState& state);

}  // namespace hibiki
