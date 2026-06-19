#pragma once

#include "types.h"

bool uci_loop(globalstate* gs);
void update_uci_opts(globalstate* gs, const std::string& opt_name, std::istringstream& remaining);
