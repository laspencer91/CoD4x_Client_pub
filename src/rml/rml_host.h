#ifndef COD4X_RML_HOST_H
#define COD4X_RML_HOST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rml_host_s {
    int (*read_file)(const char *path, void **data);
    void (*free_file)(void *data);
    void (*log)(const char *message);
    void (*play_sound)(const char *alias);
} rml_host_t;

typedef struct rml_stats_s {
    uint32_t recorded_frames, rendered_frames, draw_calls;
    uint32_t geometry_uploads, texture_uploads, pending_frames;
    int32_t last_render_result;
    int visible, activations;
} rml_stats_t;

int RmlHost_Init(const rml_host_t *host);
void RmlHost_Shutdown(void);
int RmlHost_Show(void);
void RmlHost_Hide(void);
int RmlHost_Visible(void);
int RmlHost_Reload(void);
void RmlHost_MouseMove(int x, int y);
void RmlHost_Key(int key, int down, int modifiers);
void RmlHost_Text(uint32_t codepoint);
void RmlHost_FocusLost(void);
uint32_t RmlHost_Record(int width, int height, double seconds);
void RmlHost_Discard(uint32_t ticket);
int32_t RmlHost_Render(void *device, uint32_t ticket);
void RmlHost_DeviceLost(void);
void RmlHost_Stats(rml_stats_t *stats);

#ifdef __cplusplus
}
#endif
#endif
