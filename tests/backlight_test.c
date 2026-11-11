#include "kernel_stubs.h"
#include "../mddi_lexikon_wvga.c"

int panel_type = PANEL_LEXIKON_SHARP;

struct transaction { unsigned reg, val; };
static struct transaction writes[4096];
static unsigned write_count, fail_write, errors;
static int power_refs;
static bool clocks_on, interrupt_on, hibernation_disabled;
static bool atomic_context, in_worker;
static int led_register_error, led_unregister_count;
static void (*before_lock)(void);
static void (*during_write)(void);

static void sleepable(void)
{
    assert(!atomic_context);
}

void mutex_lock(struct mutex *lock)
{
    sleepable();
    if (before_lock && lock == &panel_lock) {
        void (*hook)(void) = before_lock;
        before_lock = NULL;
        hook();
    }
    assert(!lock->locked);
    lock->locked = true;
}

void mutex_unlock(struct mutex *lock)
{
    assert(lock->locked);
    lock->locked = false;
}

void wake_lock_init(struct wake_lock *lock, int type, const char *name)
{
    (void)type;
    (void)name;
    lock->initialized = true;
    lock->refs = 0;
}

void wake_lock(struct wake_lock *lock)
{
    assert(lock->initialized);
    lock->refs++;
}

void wake_unlock(struct wake_lock *lock)
{
    assert(lock->refs > 0);
    lock->refs--;
}

bool schedule_work(struct work_struct *work)
{
    bool queued = !work->pending;
    work->pending = true;
    return queued;
}

bool cancel_work_sync(struct work_struct *work)
{
    bool pending = work->pending;
    sleepable();
    work->pending = false;
    return pending;
}

static void run_work(void)
{
    while (backlight_work.pending) {
        backlight_work.pending = false;
        in_worker = true;
        backlight_work.func(&backlight_work);
        in_worker = false;
        assert(power_refs == 0);
        assert(panel_idle_lock.refs == 0);
        assert(!panel_lock.locked);
        assert(!hibernation_disabled);
    }
}

void mdp_pipe_ctrl(int block, int state, bool isr)
{
    sleepable();
    assert(block == MDP_CMD_BLOCK && !isr);
    assert(!panel_lock.locked);
    if (state == MDP_BLOCK_POWER_ON) {
        assert(panel_idle_lock.refs == 1);
        power_refs++;
        clocks_on = interrupt_on = true;
    } else {
        assert(power_refs > 0);
        power_refs--;
        if (!power_refs)
            clocks_on = interrupt_on = false;
    }
}

void mddi_host_disable_hibernation(bool disable)
{
    sleepable();
    assert(panel_lock.locked);
    assert(clocks_on && interrupt_on);
    hibernation_disabled = disable;
}

void mddi_host_client_cnt_reset(void)
{
    assert(panel_lock.locked && clocks_on);
}

int mddi_queue_register_write(unsigned reg, unsigned val, bool wait, int sig)
{
    sleepable();
    assert(panel_lock.locked);
    assert(clocks_on && interrupt_on && hibernation_disabled);
    assert(wait && !sig);
    if (in_worker) {
        assert(power_refs > 0);
        assert(panel_idle_lock.refs == 1);
    }
    assert(write_count < ARRAY_SIZE(writes));
    writes[write_count].reg = reg;
    writes[write_count++].val = val;
    if (during_write) {
        void (*hook)(void) = during_write;
        during_write = NULL;
        hook();
    }
    return write_count == fail_write ? -EBUSY : 0;
}

void msleep(unsigned ms)
{
    sleepable();
    (void)ms;
}

int test_log(const char *format, ...)
{
    if (!strncmp(format, "ERROR: ", 7))
        errors++;
    return 0;
}

int led_classdev_register(struct device *dev, struct led_classdev *led)
{
    (void)dev;
    (void)led;
    return led_register_error;
}

void led_classdev_unregister(struct led_classdev *led)
{
    led_unregister_count++;
    led->brightness_set(led, LED_OFF);
}

int platform_device_register(struct platform_device *pdev)
{
    (void)pdev;
    return 0;
}

void platform_device_unregister(struct platform_device *pdev) { (void)pdev; }
int platform_driver_register(struct platform_driver *driver)
{
    (void)driver;
    return 0;
}
void platform_driver_unregister(struct platform_driver *driver) { (void)driver; }
void msm_fb_add_device(struct platform_device *pdev) { (void)pdev; }

static void set_brightness(enum led_brightness val)
{
    atomic_context = true;
    lexikon_backlight_led.brightness = val;
    lexikon_backlight_led.brightness_set(&lexikon_backlight_led, val);
    atomic_context = false;
}

static void reset(void)
{
    assert(power_refs == 0 && !panel_lock.locked);
    atomic_set(&bl_ready, 0);
    atomic_set(&requested_brightness, LED_FULL);
    backlight_work.pending = false;
    clocks_on = interrupt_on = hibernation_disabled = false;
    atomic_context = in_worker = false;
    before_lock = during_write = NULL;
    write_count = fail_write = errors = 0;
    led_register_error = led_unregister_count = 0;
    panel_type = PANEL_LEXIKON_SHARP;
    wake_lock_init(&panel_idle_lock, WAKE_LOCK_SUSPEND, "test");
}

static int turn_on(void)
{
    int ret;
    /* The CAF panel-on caller supplies clocks and its own power vote. */
    clocks_on = interrupt_on = true;
    ret = mddi_lexikon_panel_on(NULL);
    clocks_on = interrupt_on = false;
    return ret;
}

static void ready_panel(void)
{
    assert(turn_on() == 0);
    run_work();
    write_count = 0;
}

static void assert_brightness(unsigned start, unsigned pwm)
{
    assert(write_count == start + 2);
    assert(writes[start].reg == 0x5500 && writes[start].val == 0);
    assert(writes[start + 1].reg == 0x5100);
    assert(writes[start + 1].val == pwm);
}

static void test_idle_host_and_sweep(void)
{
    unsigned prev = 0;
    int val;
    reset();
    ready_panel();
    for (val = LED_OFF; val <= LED_FULL; val++) {
        unsigned pwm;
        write_count = 0;
        assert(!clocks_on && !interrupt_on);
        set_brightness(val);
        assert(write_count == 0);
        run_work();
        assert(write_count == 2);
        pwm = writes[1].val;
        assert(pwm <= 255 && pwm >= prev);
        prev = pwm;
        assert(lexikon_backlight_led.brightness == val);
    }
    assert(prev == 255);
    write_count = 0;
    set_brightness(76);
    run_work();
    assert_brightness(0, 48);
    puts("PASS idle clocks/IRQ, atomic LED callback, raw brightness, 256-level sweep");
}

static void shutdown_before_worker_lock(void)
{
    assert(mddi_lexikon_panel_off(NULL) == 0);
    /* CAF removes host clocks after the panel-off callback returns. */
    clocks_on = interrupt_on = false;
}

static void test_shutdown_race_and_restore(void)
{
    reset();
    ready_panel();
    set_brightness(76);
    before_lock = shutdown_before_worker_lock;
    run_work();
    assert(write_count == ARRAY_SIZE(uninit_seq));
    assert(!atomic_read(&bl_ready));
    write_count = 0;
    set_brightness(142);
    run_work();
    assert(write_count == 0 && power_refs == 0);
    assert(turn_on() == 0);
    write_count = 0;
    run_work();
    assert_brightness(0, 109);
    puts("PASS shutdown interleaving and cached brightness restore after resume");
}

static void newer_request_during_write(void)
{
    set_brightness(255);
    /* An idle power-off attempt observes the command-block reference. */
    assert(power_refs == 1 && clocks_on && interrupt_on);
}

static void test_coalescing_and_requeue(void)
{
    reset();
    ready_panel();
    set_brightness(30);
    set_brightness(83);
    set_brightness(142);
    during_write = newer_request_during_write;
    run_work();
    assert(write_count == 4);
    assert(writes[1].reg == 0x5100 && writes[1].val == 109);
    assert(writes[3].reg == 0x5100 && writes[3].val == 255);
    puts("PASS coalescing and a request queued during an active transaction");
}

static void test_initialization_and_errors(void)
{
    unsigned failure;
    reset();
    assert(!atomic_read(&bl_ready));
    set_brightness(76);
    run_work();
    assert(write_count == 0);
    fail_write = 1;
    assert(turn_on() == -EBUSY);
    assert(write_count == 1 && errors == 1);
    assert(!atomic_read(&bl_ready) && !backlight_work.pending);
    fail_write = 0;
    ready_panel();
    assert(atomic_read(&requested_brightness) == 76);
    for (failure = 1; failure <= 2; failure++) {
        write_count = errors = 0;
        fail_write = failure;
        set_brightness(83);
        run_work();
        assert(write_count == failure && errors == 1);
        assert(lexikon_backlight_led.brightness == 83);
        write_count = 0;
        fail_write = 0;
        set_brightness(83);
        run_work();
        assert_brightness(0, 55);
    }
    reset();
    panel_type = -1;
    assert(turn_on() == -ENODEV);
    assert(!atomic_read(&bl_ready) && errors == 1);
    assert(write_count == 0 && !backlight_work.pending);
    puts("PASS startup gating, init failure, unknown panel, write failures and retry");
}

static void test_panel_variants_and_off_failure(void)
{
    int variants[] = { PANEL_LEXIKON_SHARP, PANEL_LEXIKON_SHARP_CUT2,
                      PANEL_LEXIKON_SONY, PANEL_LEXIKON_SONY_CUT2 };
    unsigned i;
    for (i = 0; i < ARRAY_SIZE(variants); i++) {
        reset();
        panel_type = variants[i];
        ready_panel();
        set_brightness(76);
        run_work();
        assert_brightness(0, 48);
    }
    write_count = errors = 0;
    clocks_on = interrupt_on = true;
    fail_write = 1;
    assert(mddi_lexikon_panel_off(NULL) == -EBUSY);
    assert(!atomic_read(&bl_ready) && !hibernation_disabled);
    assert(write_count == 1 && errors == 1);
    clocks_on = interrupt_on = false;
    write_count = 0;
    set_brightness(255);
    run_work();
    assert(write_count == 0);
    puts("PASS all four panel variants and shutdown error gating");
}

static void test_registration_and_removal(void)
{
    struct platform_device pdev = { .name = "test" };
    reset();
    led_register_error = -EEXIST;
    assert(lexikon_backlight_probe(&pdev) == -EEXIST);
    assert(led_unregister_count == 0 && errors == 1);
    led_register_error = 0;
    assert(lexikon_backlight_probe(&pdev) == 0);
    ready_panel();
    set_brightness(76);
    assert(lexikon_backlight_remove(&pdev) == 0);
    assert(led_unregister_count == 1 && !backlight_work.pending);
    assert(write_count == 0);
    puts("PASS failed LED registration cleanup and pending-work cancellation");
}

int main(void)
{
    test_idle_host_and_sweep();
    test_shutdown_race_and_restore();
    test_coalescing_and_requeue();
    test_initialization_and_errors();
    test_panel_variants_and_off_failure();
    test_registration_and_removal();
    return 0;
}
