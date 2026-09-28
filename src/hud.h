/* A small bitmap-font UI engine, used by the VR menu panel (vrmenu.c) to draw
 * text and boxes into whatever framebuffer is bound - a texture hung on a
 * quad in the world, or the window itself. */
#ifndef BW_HUD_H
#define BW_HUD_H

int  hud_init(void);
void hud_shutdown(void);

/* Between begin and flush, coordinates are pixels of a `w` x `h` page, y down. */
void hud_ui_begin(int w, int h, float scale);
void hud_ui_rect(float x0, float y0, float x1, float y1, const float col[4]);
void hud_ui_frame(float x0, float y0, float x1, float y1, float lw, const float col[4]);
void hud_ui_text(float x, float y, const char *s, float scale, int align, const float col[4], int shadow);
float hud_ui_text_width(const char *s, float scale);
void hud_ui_flush(void);
#endif
