#ifndef DRAWING_MODULE_
#define DRAWING_MODULE_

// Color Format ARGB8888
#define COLOR_GREEN     0xFF00FF00
#define COLOR_BLUE      0xFF0000FF
#define COLOR_RED       0xFFFF0000
#define COLOR_YELLOW    0xFFFFFF00
#define COLOR_ORANGE    0xFFFF4500
#define COLOR_BLACK     0xFF000000
#define COLOR_WHITE     0xFFFFFFFF

#ifdef __cplusplus
extern "C" {
#endif

void draw_rectangle_yuv420sp(unsigned char* yuv420sp, int w, int h, int rx, int ry, int rw, int rh, unsigned int color, int thickness);

// void draw_rectangle_yuv420sp(unsigned char* yuv420sp, int w, int h, int stride, int rx, int ry, int rw, int rh, unsigned int color, int thickness);

void draw_image_yuv420sp(unsigned char* yuv420sp, int w, int h, unsigned char* draw_img, int rx, int ry, int rw, int rh);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif 
