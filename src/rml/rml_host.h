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
    double (*elapsed_time)(void);
    const char *(*get_value)(const char *name);
    int (*menu_response)(const char *response);
} rml_host_t;

typedef struct rml_stats_s {
    uint32_t recorded_frames, rendered_frames, draw_calls;
    uint32_t geometry_uploads, texture_uploads, pending_frames;
    int32_t last_render_result;
    int visible, activations;
    uint32_t source_reloads, source_failures;
    uint32_t record_us, render_us; /* average cost per frame since the previous call */
} rml_stats_t;

int RmlHost_Init(const rml_host_t *host);
void RmlHost_Shutdown(void);
int RmlHost_Show(void);
typedef enum { RML_SCREEN_DIAGNOSTIC, RML_SCREEN_PALETTE, RML_SCREEN_ECONOMY, RML_SCREEN_ECONOMY_PREVIEW } rml_screen_t;
int RmlHost_Open(rml_screen_t screen);
void RmlHost_Hide(void);
int RmlHost_Visible(void);
int RmlHost_Reload(void);
int RmlHost_SetSourceDirectory(const char *directory);
int RmlHost_SetModSourceDirectory(const char *directory);
/* UTF-8 folder of player images (<name key>.png or .jpg); see avatars.hpp. */
void RmlHost_SetAvatarDirectory(const char *directory);
void RmlHost_EconomyDialog(int confirmation);
int RmlHost_ActionAllowed(const char *response);
void RmlHost_MouseMove(int x, int y);
void RmlHost_Key(int key, int down, int modifiers);
void RmlHost_Text(uint32_t codepoint);
void RmlHost_FocusLost(void);
void RmlHost_ClearInput(void);
void RmlHost_EnableBroadcast(int enabled);
int RmlHost_BroadcastReady(void);
/* The passive HUD's followed-player plate is part of the last recorded frame. */
int RmlHost_BroadcastFollowShown(void);
uint32_t RmlHost_Record(int width, int height, double seconds);
void RmlHost_Discard(uint32_t ticket);
int32_t RmlHost_Render(void *device, uint32_t ticket);
void RmlHost_DeviceLost(void);
void RmlHost_Stats(rml_stats_t *stats);

#ifdef __cplusplus
}
#endif
#endif
