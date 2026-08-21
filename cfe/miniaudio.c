#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

int32_t ma_engine_size() {
  return sizeof(ma_engine);
}
int32_t ma_sound_size() {
  return sizeof(ma_sound);
}
