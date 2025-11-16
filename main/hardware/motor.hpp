esp_err_t haptic_init(void);

esp_err_t haptic_play_now(bool repeat, ...);
esp_err_t haptic_play(bool repeat, ...);
void haptic_stop(void);