#pragma once

#include "utils.h"

bool UCILoop(globalstate* gs);
void updateUCIOpts(globalstate* gs, const std::string& opt_name, std::istringstream& remaining);
void genFens(uint64_t count, uint64_t seed, uint64_t depth);

void bench();