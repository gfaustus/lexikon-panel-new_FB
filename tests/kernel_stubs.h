#ifndef LEXIKON_KERNEL_STUBS_H
#define LEXIKON_KERNEL_STUBS_H

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define __init
#define TRUE true
#define FALSE false
#define KERN_DEBUG ""
#define KERN_ERR "ERROR: "
#define THIS_MODULE NULL
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define device_initcall(fn)
#define module_init(fn)
#define WAKE_LOCK_SUSPEND 0
#define MDP_CMD_BLOCK 1
#define MDP_BLOCK_POWER_ON 1
#define MDP_BLOCK_POWER_OFF 0
#define MDDI_PANEL 1
#define DISPLAY_1 1
#define MDDI_DEFAULT_PRIM_PIX_ATTR 0

enum led_brightness { LED_OFF = 0, LED_HALF = 127, LED_FULL = 255 };
typedef struct { int value; } atomic_t;
#define ATOMIC_INIT(value) { value }
static int atomic_read(const atomic_t *value) { return value->value; }
static void atomic_set(atomic_t *value, int next) { value->value = next; }

struct mutex { bool locked; };
#define DEFINE_MUTEX(name) struct mutex name
void mutex_lock(struct mutex *lock);
void mutex_unlock(struct mutex *lock);

struct wake_lock { int refs; bool initialized; };
void wake_lock_init(struct wake_lock *lock, int type, const char *name);
void wake_lock(struct wake_lock *lock);
void wake_unlock(struct wake_lock *lock);

struct work_struct {
    void (*func)(struct work_struct *);
    bool pending;
};
#define DECLARE_WORK(name, callback) \
    struct work_struct name = { .func = callback }
bool schedule_work(struct work_struct *work);
bool cancel_work_sync(struct work_struct *work);

struct device { void *platform_data; };
struct platform_device { const char *name; int id; struct device dev; };
struct platform_driver {
    int (*probe)(struct platform_device *);
    int (*remove)(struct platform_device *);
    struct { const char *name; void *owner; } driver;
};
int platform_device_register(struct platform_device *pdev);
void platform_device_unregister(struct platform_device *pdev);
int platform_driver_register(struct platform_driver *driver);
void platform_driver_unregister(struct platform_driver *driver);

struct led_classdev {
    const char *name;
    int brightness;
    void (*brightness_set)(struct led_classdev *, enum led_brightness);
};
int led_classdev_register(struct device *dev, struct led_classdev *led);
void led_classdev_unregister(struct led_classdev *led);

struct msm_panel_info {
    unsigned xres, yres, type, pdest, wait_cycle, bpp, fb_num;
    unsigned clk_rate, clk_min, clk_max;
    struct {
        unsigned rev, v_back_porch, v_front_porch, v_pulse_width;
    } lcd;
    struct { unsigned vdopkt; } mddi;
};
struct msm_fb_panel_data {
    int (*on)(struct platform_device *);
    int (*off)(struct platform_device *);
    struct msm_panel_info panel_info;
};
void msm_fb_add_device(struct platform_device *pdev);
void mdp_pipe_ctrl(int block, int state, bool isr);
void mddi_host_disable_hibernation(bool disable);
void mddi_host_client_cnt_reset(void);
int mddi_queue_register_write(unsigned reg, unsigned val, bool wait, int sig);
void msleep(unsigned ms);
int test_log(const char *format, ...);
#define printk test_log
#define pr_err(...) test_log("ERROR: " __VA_ARGS__)
#define pr_info test_log

#endif
