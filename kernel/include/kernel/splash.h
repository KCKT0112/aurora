/* Aurora v2 - first-light splash screen (temporary kernel-side surface).
 *
 * This validates the 32-bit RGB framebuffer path and sets the visual language
 * for the user-space compositor that will replace it in the next milestone.
 */
#ifndef AURORA_KERNEL_SPLASH_H
#define AURORA_KERNEL_SPLASH_H

void splash_render(void);

#endif /* AURORA_KERNEL_SPLASH_H */
