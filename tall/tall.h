/* Snes9x Tall renderer. Original Snes9x license applies; see LICENSE. */
#ifndef SFC_TALL_H
#define SFC_TALL_H
void S9xTallConfigure(unsigned height, unsigned layers, bool sprites);
unsigned S9xTallHeight();
const uint16 *S9xTallCompose(unsigned width, unsigned height);
#endif
