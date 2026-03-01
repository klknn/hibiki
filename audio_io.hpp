#pragma once
#include <string>
#include <vector>

bool loadWav(const std::string& path, std::vector<float>& out_data, int& out_channels, double& out_duration);
