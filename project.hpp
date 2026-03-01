#pragma once
#include <string>

struct GlobalState;

void save_project(GlobalState& state, const std::string& path);
void load_project(GlobalState& state, const std::string& path);
