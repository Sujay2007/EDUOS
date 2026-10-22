# VirtualBox BIOS boot: verified diagnosis and configuration

## Cause of the Limine panic

The inspected `Education OS` VM (VirtualBox 7.2.22 on Windows) had guest long
mode disabled. Its machine-readable configuration reported `longmode="off"`;
the saved XML contained `<LongMode enabled="false"/>`. The failed boot's
`VBox.log` recorded:

```text
Guest OS type: 'Other'
Enable64bit = 0
Gst: 80000001/0000  00a40f41 40000000 000001f1 0bd3fb7f
LM - AMD64 Long Mode = 0 (1)
```

The last line means the guest lacks LM while the host supports it. PAE/NX,
nested paging, host virtualization and KVM paravirtualization do not enable the
guest's independent long-mode setting.

The bundled Limine is the unmodified v8.7.0 binary release, commit
`aad3edd370955449717a334f0289dee10e2c5f01`. Its BIOS-stage binary contains the
reported panic text. The matching [Limine source](https://github.com/Limine-Bootloader/Limine/blob/v8.7.0/common/protos/limine.c#L545-L549)
checks CPUID leaf `0x80000001`, EDX bit 29, and issues that panic when long mode
is absent, before transferring control to the kernel. This identifies a guest
CPU-feature configuration failure, not a kernel entry-point or ISO-format error.

## Changes actually applied

1. Backed up the VM XML, failed-boot log and original machine-readable settings
   under `Diagnostics-before-long-mode-20261020-234401` in the VM directory.
2. Enabled `--x86-long-mode on`. A new log confirmed `Enable64bit = 1` and
   `LM - AMD64 Long Mode = 1 (1)`; the CPU panic disappeared.
3. Enabled COM1 at `0x3f8`, IRQ 4, with a file backend. QEMU already supplies
   COM1 through `-serial stdio`; the VirtualBox VM initially had UART1 disabled.
   The existing kernel assumes this UART exists and polls it during boot/input
   setup. With COM1 enabled, boot reached the graphical shell.
4. Preserved the originally attached Windows ISO. Its hash differed from the
   current repository ISO. Copied the current ISO to a separate file in the
   diagnostics directory and attached that verified copy for the final boot.
   The original ISO also reached the shell after the configuration correction.

No kernel, Limine, build-script or test changes were necessary. No ISO rebuild
was justified. The existing project ISO and `disk.img` were not replaced.

Current verified ISO SHA-256:

```text
562b25ace15a229ad16252da587c2e84d687a723ddeeef0c5eb23a5cecece394
```

The build uses xorriso BIOS and UEFI El Torito images plus Limine BIOS install.
Both kernel copies are identical ELF64 x86-64 executables with entry
`0xffffffff80000000`; staged Limine BIOS binaries match the bundled binaries.
The ISO has both boot catalog entries. The kernel configuration correctly uses
`protocol: limine` and `path: boot():/boot/kernel.elf`.

## Repeat the VirtualBox test

The VM was returned to **powered off**, retaining the corrected settings and
the verified ISO attachment. In PowerShell, start its graphical frontend:

```powershell
& "$env:ProgramFiles\Oracle\VirtualBox\VBoxManage.exe" startvm "Education OS" --type gui
```

Wait for Limine and then the `edus>` graphical shell. Type `run hello` and press
Enter. The launch PID should match the Ring 3 program's printed PID, with exit
status 0. This exact workflow was exercised using VirtualBox PS/2 key injection
and checked through a screenshot and COM1 log.

To reapply the essential settings while the VM is fully powered off:

```powershell
$vbox = "$env:ProgramFiles\Oracle\VirtualBox\VBoxManage.exe"
$serialLog = "$env:USERPROFILE\VirtualBox VMs\Education OS\serial.log"
& $vbox modifyvm "Education OS" --x86-long-mode on --uart1 0x3f8 4 --uart-mode1 file $serialLog
& $vbox showvminfo "Education OS" --machinereadable
```

These flags are from the installed 7.2 CLI and [Oracle's reference](https://docs.oracle.com/en/virtualization/virtualbox/7.2/user/vboxmanage.html).
For a new VM, choose a 64-bit guest type and verify long mode explicitly. Keep
BIOS firmware, one CPU and PAE/NX enabled. The existing 512 MiB RAM and VBoxVGA
configuration produced a validated 1024x768, 32-bit framebuffer; no EFI switch
or paravirtualization-provider change was required.

The validated attachment on this host is:

```text
C:\Users\Sujay Krishna R\VirtualBox VMs\Education OS\Diagnostics-before-long-mode-20261020-234401\educational-os-current.iso
```

The same directory contains before/after settings, logs and
`current-iso-shell.png`. The original Windows ISO remains at
`C:\Users\Sujay Krishna R\Documents\OS ISO file\educational-os.iso`.

## Observed limits

This verifies VirtualBox BIOS boot, the graphical/live shell and the isolated
`hello` application. It is not a claim that the full VirtualBox test suite passed:

* The kernel's preemption test reported zero timer-driven preemptions during its
  measured workload and printed FAIL. Later user-mode, process and live checks
  passed. The timing-sensitive test was preserved without weakening it.
* No persistent data disk is attached to this VM. The kernel explicitly skipped
  persistent-disk checks; filesystem parsing tests alone are not disk validation.
  The original QEMU setup still supplies the persistent ATA disk.
* The Windows log uses the NEM/Windows hypervisor backend. Long mode worked after
  enabling it without changing host virtualization/security settings.

QEMU regression command, unchanged:

```sh
python3 tools/test_final.py
```

The regression was rerun after the VM configuration fix and reported FINAL PASS,
including graphics/live/application checks and both persistent-storage boots.
The project ISO hash above and the persistent `disk.img` hash were unchanged.
