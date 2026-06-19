#include "nnue.h"
INCBIN(nnue_bin, NNUE_PATH);
const NetworkWeights* LOADED_NNUE = (NetworkWeights*)gnnue_binData;
