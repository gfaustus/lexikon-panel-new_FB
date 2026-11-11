# lexikon-panel-new_FB
lexikon panel & backlight driver for CAF kernel

The driver supports panel on/off and deferred backlight brightness updates for the Sharp and Sony Lexikon panels, including CUT2 variants.

Brightness requests run in a workqueue with an MDP command-block power reference and a suspend wake lock. Panel initialization and shutdown serialize with register writes, and the latest requested brightness is restored after panel initialization. MDDI write failures are logged and panel initialization/shutdown errors propagate to the framebuffer.

## Validation

Run the host-side regression fixture with Python 3 and a C compiler:

```sh
python3 tests/run.py
```

The fixture compiles the complete panel driver with warnings as errors and AddressSanitizer/UndefinedBehaviorSanitizer. It exercises all 256 brightness levels, clock-gated host updates, atomic LED callbacks, shutdown interleavings, request coalescing, resume restoration, transport failures, and all four supported panel variants.

The power API follows the CAF MSM framebuffer implementation in [Andromadus/htc7x30-3.0 at c2e102a6](https://github.com/Andromadus/htc7x30-3.0/tree/c2e102a6b3e68c5b4d92de7a03da653d1416b7b7/drivers/video/msm). Integration validation requires building in the target CAF kernel and testing on an HTC Lexikon: adjust brightness after the display has idled, sweep the brightness range, and repeat screen-off/on cycles while changing brightness. Capture `/proc/last_kmsg` or ramoops if the device reboots.
