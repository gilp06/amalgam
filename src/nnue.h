#pragma once

#include <cstddef>
#include <cstdint>

#include <immintrin.h>
#include <incbin.h>

#define NNUE_PATH "../nnue/led-40-1024.bin"
#define HIDDEN_SIZE 1024
#define SCALE 400
#define BUCKET_COUNT 1
#define BUCKET_DIV ((32 + BUCKET_COUNT - 1) / BUCKET_COUNT)
#define QA 255
#define QB 64

INCBIN_EXTERN(nnue_bin);

struct alignas(64) Accumulator {
    int16_t values[HIDDEN_SIZE];
};

struct NetworkWeights {
  Accumulator feature_weights[768];
  Accumulator feature_biases;

  int16_t output_weights[BUCKET_COUNT][2 * HIDDEN_SIZE];
  int16_t output_bias[BUCKET_COUNT];
};

extern const NetworkWeights *LOADED_NNUE;

class NNUE {
public:
  static inline int32_t clamp_mag(int32_t min, int32_t max, int32_t value) {
    if (value < min)
      return min;
    if (value > max)
      return max;
    return value;
  }

  static inline int32_t screlu(int32_t x) {
    int32_t y = clamp_mag(0, QA, x);
    return y * y;
  }

  static inline void accum_add_feat(int16_t *inputs, int16_t *outputs, size_t index) {
    
    auto buff = LOADED_NNUE->feature_weights[index].values;
    for (int i = 0; i < HIDDEN_SIZE; i++) {
      outputs[i] = inputs[i] + buff[i];
    }
  }

  static inline void accum_remove_feat(int16_t *inputs, int16_t *outputs, size_t index) {
    auto buff = LOADED_NNUE->feature_weights[index].values;
    for (int i = 0; i < HIDDEN_SIZE; i++) {
      outputs[i] = inputs[i] - buff[i];
    }
  }

  static inline void accum_add_sub_feats(int16_t *inputs, int16_t *outputs, size_t add_index, size_t sub_index) {
    auto buff_add = LOADED_NNUE->feature_weights[add_index].values;
    auto buff_sub = LOADED_NNUE->feature_weights[sub_index].values;
    for (int i = 0; i < HIDDEN_SIZE; i++) {
      outputs[i] = inputs[i] + buff_add[i] - buff_sub[i];
    }
  }

  static inline void accum_add_sub2_feats(int16_t *inputs, int16_t *outputs, size_t add_index, size_t sub1_index, size_t sub2_index) {
    auto buff_add = LOADED_NNUE->feature_weights[add_index].values;
    auto buff_sub1 = LOADED_NNUE->feature_weights[sub1_index].values;
    auto buff_sub2 = LOADED_NNUE->feature_weights[sub2_index].values;
    for (int i = 0; i < HIDDEN_SIZE; i++) {
      outputs[i] = inputs[i] + buff_add[i] - buff_sub1[i] - buff_sub2[i];
    }
  }

  static inline void accum_add2_sub2_feats(int16_t *inputs, int16_t *outputs, size_t add1_index, size_t add2_index, size_t sub1_index, size_t sub2_index) {
    auto buff_add1 = LOADED_NNUE->feature_weights[add1_index].values;
    auto buff_add2 = LOADED_NNUE->feature_weights[add2_index].values;
    auto buff_sub1 = LOADED_NNUE->feature_weights[sub1_index].values;
    auto buff_sub2 = LOADED_NNUE->feature_weights[sub2_index].values;
    for (int i = 0; i < HIDDEN_SIZE; i++) {
      outputs[i] = inputs[i] + buff_add1[i] + buff_add2[i] - buff_sub1[i] - buff_sub2[i];
    }
  }

  static inline size_t get_bucket(size_t piece_count) {
    size_t bucket = (piece_count - 2) / BUCKET_DIV;

    if (bucket >= BUCKET_COUNT)
      return BUCKET_COUNT - 1;

    return bucket;
  }

  static inline int32_t evaluate(Accumulator *us, Accumulator *them,
                                 size_t piece_count) {
    const size_t bucket = get_bucket(piece_count);

    const int16_t *us_values = us->values;
    const int16_t *them_values = them->values;

    const int16_t *us_weights = LOADED_NNUE->output_weights[bucket];
    const int16_t *them_weights = LOADED_NNUE->output_weights[bucket] + HIDDEN_SIZE;

    const __m512i vec_zero = _mm512_setzero_si512();
    const __m512i vec_qa = _mm512_set1_epi16(QA);

    __m512i sum = vec_zero;

    for (int i = 0; i < HIDDEN_SIZE; i += 32) {
      const __m512i usv = _mm512_load_si512((const __m512i *)(us_values + i));
      const __m512i themv =
          _mm512_load_si512((const __m512i *)(them_values + i));

      const __m512i usw = _mm512_load_si512((const __m512i *)(us_weights + i));
      const __m512i themw =
          _mm512_load_si512((const __m512i *)(them_weights + i));

      const __m512i us_clamped =
          _mm512_min_epi16(_mm512_max_epi16(usv, vec_zero), vec_qa);

      const __m512i them_clamped =
          _mm512_min_epi16(_mm512_max_epi16(themv, vec_zero), vec_qa);

      const __m512i us_results =
          _mm512_madd_epi16(_mm512_mullo_epi16(usw, us_clamped), us_clamped);

      const __m512i them_results = _mm512_madd_epi16(
          _mm512_mullo_epi16(themw, them_clamped), them_clamped);

      sum = _mm512_add_epi32(sum, us_results);
      sum = _mm512_add_epi32(sum, them_results);
    }

    int32_t output = _mm512_reduce_add_epi32(sum);

    output /= QA;
    output += LOADED_NNUE->output_bias[bucket];
    output *= SCALE;
    output /= (QA * QB);

    return output;
  }
};
