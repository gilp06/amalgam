#pragma once

#include "types.h"

bool UCILoop(globalstate* gs);
void updateUCIOpts(globalstate* gs, const std::string& opt_name, std::istringstream& remaining);
