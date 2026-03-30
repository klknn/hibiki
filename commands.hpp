#pragma once

#include "history.hpp"
#include "pb/commands.pb.h"
#include "project.hpp"

namespace hibiki {

void handleProjectCmd(const pb::commands::ProjectCmd& cmd, ProjectState& state,
                      HistoryManager& history);
void handleTransportCmd(const pb::commands::TransportCmd& cmd,
                        ProjectState& state);
void handleTrackCmd(const pb::commands::TrackCmd& cmd, ProjectState& state,
                    HistoryManager& history);
void handlePluginCmd(const pb::commands::PluginCmd& cmd, ProjectState& state,
                     HistoryManager& history);
void handleAutomationCmd(const pb::commands::AutomationCmd& cmd,
                         ProjectState& state, HistoryManager& history);
void handleMidiCmd(const pb::commands::MidiCmd& cmd, ProjectState& state,
                   HistoryManager& history);

}  // namespace hibiki
