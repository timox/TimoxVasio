#pragma once
#include "device_transaction.h"
#include <string>
#include <vector>
namespace timox::lab {
struct ParsedCommand { bool valid = false; bool help = false; Request request; };
ParsedCommand ParseCommand(const std::vector<std::wstring>& arguments);
}
