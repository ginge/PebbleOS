# Coordinated CP sleep investigation

CP deep sleep is deferred. Production AP retained idle keeps the CP APP sleep
veto held and does not change shared clocks, voltage or memory power states.
CP low-voltage modes are available only in bounded diagnostic configurations.

The investigation is preserved in [the CP history](coordinated-sleep-history.md)
and [the consolidated power history](power-investigation-history.md). Raw UART,
phone state, exact builds and first-failure captures remain in reference-workspace
`artifacts/deep-sleep-review-20261101/`.

The strongest observations were:

- CP-awake AP deep-WFI controls returned successfully. On 8 November the current
  bounded path completed12 returns with zero abort/deadline/contract faults;
  a continuous combined image subsequently reported32 returns.
- CP mask-only sequences reproduced failed QMI/ADC service without CP WFI.
  AP175 captured I2C IRQ and ACK asserted but no forwarded pending interrupt.
  This narrows the failure to the suspend/mask/cleanup sequence; it does not
  identify a single causal store or justify disabling shared interrupt guards.
- Real coordinated trials reset without a confirmed returned-WFI boundary.
  AP176's last committed CP stage was19. A committed marker is not exact-PC or
  PMU residency evidence. No coordinated CP sleep or saving is qualified.
- Bluetooth failures also occurred outside sleep slots and after normal warm
  restoration. Preserve aligned AP/CP/phone evidence; no single cause is proven.

Do not remove the APP veto, reenable fixed120 MHz, issue AP-only shared-flash
power-down, or substitute a successful configuration/query for residency proof.
Use the [AP-only path](ap-only-sleep.md) and [current TODO](power-todo.md).
