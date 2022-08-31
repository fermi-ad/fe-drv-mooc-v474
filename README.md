# V474 Driver for MOOC

The V474 is a VME card that controls four power supplies. Its
documentation can be found
[here](https://www-bd.fnal.gov/controls/vme_modules/V474.pdf). This
driver is built against MOOC 4.6.

Changes in v1.5:

- Uses v3.0 of the [VXPP library](https://github.com/fermi-controls/vxpp/wiki).
  Specifically, portions were rewritten to use the
  [`VME::Memory`](https://github.com/fermi-controls/vxpp/wiki/Memory-Spaces)
  templates.
- Uses error logger instead of `printf`.
- Uses front panel LEDs to indicate state of channels.
- Bug fix: use `volatile` pointer to hardware.

# Location

Our development environment supports quite a few combinations of hardware
and software platforms. Be sure to download from the appropriate area.

| BSP    |VxWorks | File                                           |
|--------|--------|------------------------------------------------|
| mv2401 | 6.4    | `vxworks_boot/v6.4/module/mv2401/v474-1.5.out` |
| mv2434 | 6.4    | `vxworks_boot/v6.4/module/mv2434/v474-1.5.out` |
| mv5500 | 6.4    | `vxworks_boot/v6.4/module/mv5500/v474-1.5.out` |
