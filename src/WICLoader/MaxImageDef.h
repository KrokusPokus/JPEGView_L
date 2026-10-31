#pragma once

// That must not be bigger than 65535 due to internal limitations
const unsigned int MAX_IMAGE_DIMENSION = 65535;

// Must be the same as in JPEGView\MaxImageDef.h
#ifdef _WIN64
const unsigned int MAX_IMAGE_PIXELS = (unsigned)MAX_IMAGE_DIMENSION * MAX_IMAGE_DIMENSION;
#else
const unsigned int MAX_IMAGE_PIXELS = 1024 * 1024 * 100;
#endif