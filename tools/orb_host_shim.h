#pragma once
#include <math.h>
#include <stdint.h>
static inline int16_t sin16(uint16_t t){ return (int16_t)lrint(sin(t*2.0*M_PI/65536.0)*32767.0); }
static inline int16_t cos16(uint16_t t){ return (int16_t)lrint(cos(t*2.0*M_PI/65536.0)*32767.0); }
