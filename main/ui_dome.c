// Dome Control screen - shows dome status and OPEN/CLOSE buttons
// Sends commands to AstroShell controller + Pushover notifications
// v0.5.0

#include "ui_dome.h"
#include "ui_main.h"
#include "nina_client.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ui_dome";

// Colors (matching project palette)
#define COLOR_GREEN     lv_color_hex(0x00c853)
#define COLOR_RED       lv_color_hex(0xff4444)
#define COLOR_YELLOW    lv_color_hex(0xffab00)
#define COLOR_TEXT_DIM  lv_color_hex(0x8899aa)
#define COLOR_BG_DARK   lv_color_hex(0x0a0a1a)

// Dome status label and banner
static lv_obj_t *dome_status_banner = NULL;
static lv_obj_t *lbl_dome_status = NULL;

// Action buttons
static lv_obj_t *btn_close_all = NULL;
static lv_obj_t *btn_open_all = NULL;

// Feedback label (shows command result)
static lv_obj_t *lbl_feedback = NULL;

// Confirmation dialog for OPEN (appears 2s after tapping OPEN ALL)
static lv_obj_t *confirm_msgbox = NULL;

// Timer for delayed confirmation dialog
static lv_timer_t *confirm_delay_timer = NULL;

// FreeRTOS task to execute dome command without blocking LVGL
static void dome_cmd_task(void *arg)
{
    bool is_open = (bool)(intptr_t)arg;

    // Send dome command
    esp_err_t err = dome_send_command(is_open);

    // Send Pushover notification
    const char *title = is_open ? "Dome Control" : "Dome Control";
    const char *msg = is_open ? "Dome OPENED by waveshare" : "Dome CLOSED by waveshare";
    pushover_send(title, msg);

    // Update feedback label on UI thread
    if (lbl_feedback) {
        // Need LVGL lock to update UI
        extern bool bsp_display_lock(uint32_t timeout_ms);
        extern void bsp_display_unlock(void);
        if (bsp_display_lock(200)) {
            if (err == ESP_OK) {
                lv_label_set_text(lbl_feedback, is_open ? "OPEN command sent" : "CLOSE command sent");
                lv_obj_set_style_text_color(lbl_feedback, is_open ? COLOR_RED : COLOR_GREEN, 0);
            } else {
                lv_label_set_text(lbl_feedback, "Command FAILED!");
                lv_obj_set_style_text_color(lbl_feedback, COLOR_YELLOW, 0);
            }
            bsp_display_unlock();
        }
    }

    ESP_LOGI(TAG, "Dome %s command task finished: %s",
             is_open ? "OPEN" : "CLOSE", esp_err_to_name(err));

    // Wait 5 seconds then re-query dome status to refresh the UI
    vTaskDelay(pdMS_TO_TICKS(5000));
    nina_dome_status_t status;
    if (nina_fetch_dome_status(&status) == ESP_OK) {
        ui_update_dome_status(&status);
    }

    vTaskDelete(NULL);
}

// Execute dome command in a background task (non-blocking)
static void execute_dome_command(bool is_open)
{
    lv_label_set_text(lbl_feedback, is_open ? "Opening dome..." : "Closing dome...");
    lv_obj_set_style_text_color(lbl_feedback, COLOR_YELLOW, 0);

    // Launch command in separate FreeRTOS task to avoid blocking LVGL
    xTaskCreate(dome_cmd_task, "dome_cmd", 8192, (void *)(intptr_t)is_open, 5, NULL);
}

// CLOSE ALL button callback - immediate, no confirmation needed
static void close_btn_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "CLOSE ALL pressed");
    execute_dome_command(false);
}

// Close the confirmation dialog and clean up
static void close_confirm_dialog(void)
{
    if (confirm_msgbox) {
        lv_obj_delete(confirm_msgbox);
        confirm_msgbox = NULL;
    }
}

// "Yes" button callback - confirm OPEN
static void confirm_yes_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "OPEN confirmed by user");
    close_confirm_dialog();
    execute_dome_command(true);
}

// "No" button callback - cancel OPEN
static void confirm_no_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "OPEN cancelled by user");
    close_confirm_dialog();
    lv_label_set_text(lbl_feedback, "Open cancelled");
    lv_obj_set_style_text_color(lbl_feedback, COLOR_TEXT_DIM, 0);
}

// Timer callback - shows confirmation dialog 2 seconds after OPEN ALL was pressed
static void confirm_delay_cb(lv_timer_t *timer)
{
    (void)timer;

    // Delete the one-shot timer
    if (confirm_delay_timer) {
        lv_timer_delete(confirm_delay_timer);
        confirm_delay_timer = NULL;
    }

    // Don't create another if one is already showing
    if (confirm_msgbox) return;

    // Create modal confirmation overlay on the active screen
    confirm_msgbox = lv_obj_create(lv_screen_active());
    lv_obj_set_size(confirm_msgbox, 420, 300);
    lv_obj_center(confirm_msgbox);
    lv_obj_set_style_bg_color(confirm_msgbox, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(confirm_msgbox, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(confirm_msgbox, COLOR_RED, 0);
    lv_obj_set_style_border_width(confirm_msgbox, 3, 0);
    lv_obj_set_style_radius(confirm_msgbox, 16, 0);
    lv_obj_clear_flag(confirm_msgbox, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t *dlg_title = lv_label_create(confirm_msgbox);
    lv_label_set_text(dlg_title, "Confirm OPEN ALL");
    lv_obj_set_style_text_font(dlg_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(dlg_title, COLOR_RED, 0);
    lv_obj_align(dlg_title, LV_ALIGN_TOP_MID, 0, 15);

    // Question text
    lv_obj_t *dlg_text = lv_label_create(confirm_msgbox);
    lv_label_set_text(dlg_text, "Are you sure you want to\nOPEN ALL shutters?");
    lv_obj_set_style_text_font(dlg_text, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(dlg_text, lv_color_white(), 0);
    lv_obj_set_style_text_align(dlg_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(dlg_text, LV_ALIGN_CENTER, 0, -20);

    // Yes button (red, dangerous)
    lv_obj_t *btn_yes = lv_btn_create(confirm_msgbox);
    lv_obj_set_size(btn_yes, 140, 55);
    lv_obj_align(btn_yes, LV_ALIGN_BOTTOM_LEFT, 30, -20);
    lv_obj_set_style_bg_color(btn_yes, lv_color_hex(0x4a0a0a), 0);
    lv_obj_set_style_bg_color(btn_yes, lv_color_hex(0x6b1a1a), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_yes, 2, 0);
    lv_obj_set_style_border_color(btn_yes, COLOR_RED, 0);
    lv_obj_set_style_radius(btn_yes, 12, 0);
    lv_obj_t *lbl_yes = lv_label_create(btn_yes);
    lv_label_set_text(lbl_yes, "Yes");
    lv_obj_set_style_text_font(lbl_yes, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_yes, COLOR_RED, 0);
    lv_obj_center(lbl_yes);
    lv_obj_add_event_cb(btn_yes, confirm_yes_cb, LV_EVENT_CLICKED, NULL);

    // No button (neutral)
    lv_obj_t *btn_no = lv_btn_create(confirm_msgbox);
    lv_obj_set_size(btn_no, 140, 55);
    lv_obj_align(btn_no, LV_ALIGN_BOTTOM_RIGHT, -30, -20);
    lv_obj_set_style_bg_color(btn_no, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(btn_no, lv_color_hex(0x555555), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_no, 2, 0);
    lv_obj_set_style_border_color(btn_no, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_radius(btn_no, 12, 0);
    lv_obj_t *lbl_no = lv_label_create(btn_no);
    lv_label_set_text(lbl_no, "No");
    lv_obj_set_style_text_font(lbl_no, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_no, lv_color_white(), 0);
    lv_obj_center(lbl_no);
    lv_obj_add_event_cb(btn_no, confirm_no_cb, LV_EVENT_CLICKED, NULL);
}

// OPEN ALL button callback - starts 2s delay before showing confirmation
static void open_btn_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "OPEN ALL pressed, waiting 2s for confirmation...");

    lv_label_set_text(lbl_feedback, "Confirm opening in 2s...");
    lv_obj_set_style_text_color(lbl_feedback, COLOR_YELLOW, 0);

    // Cancel any existing timer
    if (confirm_delay_timer) {
        lv_timer_delete(confirm_delay_timer);
    }

    // Create one-shot timer for 2-second delay
    confirm_delay_timer = lv_timer_create(confirm_delay_cb, 2000, NULL);
    lv_timer_set_repeat_count(confirm_delay_timer, 1);
}

lv_obj_t *ui_dome_create(lv_obj_t *parent)
{
    // Screen title
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Dome Control");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    // --- Dome status banner (large, centered) ---
    dome_status_banner = lv_obj_create(parent);
    lv_obj_set_size(dome_status_banner, 500, 100);
    lv_obj_align(dome_status_banner, LV_ALIGN_TOP_MID, 0, 65);
    lv_obj_set_style_bg_color(dome_status_banner, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_radius(dome_status_banner, 16, 0);
    lv_obj_set_style_border_width(dome_status_banner, 3, 0);
    lv_obj_set_style_border_color(dome_status_banner, COLOR_TEXT_DIM, 0);
    lv_obj_clear_flag(dome_status_banner, LV_OBJ_FLAG_SCROLLABLE);

    lbl_dome_status = lv_label_create(dome_status_banner);
    lv_label_set_text(lbl_dome_status, "DOME --");
    lv_obj_set_style_text_font(lbl_dome_status, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(lbl_dome_status, COLOR_TEXT_DIM, 0);
    lv_obj_center(lbl_dome_status);

    // --- CLOSE ALL button (green, safe action - no confirmation needed) ---
    btn_close_all = lv_btn_create(parent);
    lv_obj_set_size(btn_close_all, 280, 120);
    lv_obj_align(btn_close_all, LV_ALIGN_CENTER, -155, 30);
    lv_obj_set_style_bg_color(btn_close_all, lv_color_hex(0x0a4a2a), 0);
    lv_obj_set_style_bg_color(btn_close_all, lv_color_hex(0x0d6b3a), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_close_all, 16, 0);
    lv_obj_set_style_border_width(btn_close_all, 3, 0);
    lv_obj_set_style_border_color(btn_close_all, COLOR_GREEN, 0);
    lv_obj_set_style_shadow_width(btn_close_all, 20, 0);
    lv_obj_set_style_shadow_color(btn_close_all, lv_color_hex(0x004d1a), 0);

    lv_obj_t *close_lbl = lv_label_create(btn_close_all);
    lv_label_set_text(close_lbl, "CLOSE\nALL");
    lv_obj_set_style_text_font(close_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(close_lbl, COLOR_GREEN, 0);
    lv_obj_set_style_text_align(close_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(btn_close_all, close_btn_cb, LV_EVENT_CLICKED, NULL);

    // --- OPEN ALL button (red, dangerous action - needs confirmation) ---
    btn_open_all = lv_btn_create(parent);
    lv_obj_set_size(btn_open_all, 280, 120);
    lv_obj_align(btn_open_all, LV_ALIGN_CENTER, 155, 30);
    lv_obj_set_style_bg_color(btn_open_all, lv_color_hex(0x4a0a0a), 0);
    lv_obj_set_style_bg_color(btn_open_all, lv_color_hex(0x6b1a1a), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_open_all, 16, 0);
    lv_obj_set_style_border_width(btn_open_all, 3, 0);
    lv_obj_set_style_border_color(btn_open_all, COLOR_RED, 0);
    lv_obj_set_style_shadow_width(btn_open_all, 20, 0);
    lv_obj_set_style_shadow_color(btn_open_all, lv_color_hex(0x4d0000), 0);

    lv_obj_t *open_lbl = lv_label_create(btn_open_all);
    lv_label_set_text(open_lbl, "OPEN\nALL");
    lv_obj_set_style_text_font(open_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(open_lbl, COLOR_RED, 0);
    lv_obj_set_style_text_align(open_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(open_lbl);
    lv_obj_add_event_cb(btn_open_all, open_btn_cb, LV_EVENT_CLICKED, NULL);

    // --- Feedback label (shows command status) ---
    lbl_feedback = lv_label_create(parent);
    lv_label_set_text(lbl_feedback, "");
    lv_obj_set_style_text_font(lbl_feedback, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_feedback, COLOR_TEXT_DIM, 0);
    lv_obj_align(lbl_feedback, LV_ALIGN_CENTER, 0, 130);

    // Info text at bottom
    lv_obj_t *info = lv_label_create(parent);
    lv_label_set_text(info, "AstroShell @ " CONFIG_DOME_CONTROLLER_IP);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(info, lv_color_hex(0x555555), 0);
    lv_obj_align(info, LV_ALIGN_CENTER, 0, 170);

    ESP_LOGI(TAG, "Dome control screen created");
    return parent;
}

void ui_dome_update(const nina_dome_status_t *dome)
{
    if (!dome || !lbl_dome_status) return;

    if (!dome->valid) {
        lv_label_set_text(lbl_dome_status, "DOME --");
        lv_obj_set_style_text_color(lbl_dome_status, COLOR_TEXT_DIM, 0);
        lv_obj_set_style_border_color(dome_status_banner, COLOR_TEXT_DIM, 0);
        lv_obj_set_style_bg_color(dome_status_banner, lv_color_hex(0x1a1a2e), 0);
        return;
    }

    if (strcasecmp(dome->shutter_status, "CLOSED") == 0) {
        lv_label_set_text(lbl_dome_status, "CLOSED");
        lv_obj_set_style_text_color(lbl_dome_status, COLOR_GREEN, 0);
        lv_obj_set_style_border_color(dome_status_banner, COLOR_GREEN, 0);
        lv_obj_set_style_bg_color(dome_status_banner, lv_color_hex(0x0a2a1a), 0);
    } else if (strcasecmp(dome->shutter_status, "OPEN") == 0) {
        lv_label_set_text(lbl_dome_status, "OPEN");
        lv_obj_set_style_text_color(lbl_dome_status, COLOR_RED, 0);
        lv_obj_set_style_border_color(dome_status_banner, COLOR_RED, 0);
        lv_obj_set_style_bg_color(dome_status_banner, lv_color_hex(0x2a0a0a), 0);
    } else {
        lv_label_set_text(lbl_dome_status, "Domestatus?");
        lv_obj_set_style_text_color(lbl_dome_status, COLOR_YELLOW, 0);
        lv_obj_set_style_border_color(dome_status_banner, COLOR_YELLOW, 0);
        lv_obj_set_style_bg_color(dome_status_banner, lv_color_hex(0x2a2a0a), 0);
    }
}
