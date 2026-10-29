# Login banner: port roles, current switch state and how to change it.
# Sourced by /etc/profile for every login shell (UART and SSH). Interactive
# shells only, so that "ssh host command" and scp stay quiet.

if [ -n "$PS1" ]; then
	cat <<'EOF'

 DSL-2542B passive tap
 ---------------------------------------------------------------------
  LAN1  configuration: SSH root@192.168.15.1, DHCP 192.168.15.100-150
  LAN2  capture: Wireshark laptop, copy of LAN4 traffic (rx+tx)
  LAN3  far side of the tapped link  <-- switched in hardware -->
  LAN4  device under observation
 ---------------------------------------------------------------------
 Change the configuration:
   vi /etc/swmirror.conf               port isolation + mirroring
   /etc/init.d/S38swmirror restart     apply it
 Or directly (until reboot):
   swmirror status                     full switch state
   swmirror on lan2 lan4 both          mirror LAN4 rx+tx to LAN2
   swmirror pvlan lan3 lan4            LAN3 may forward only to LAN4
   swmirror off                        disable mirroring
 Careful: keep PVLAN_LAN1=cpu and PVLAN_CPU=lan1, or SSH stops working
 (the UART console always works).

 The root filesystem lives in RAM: every change, including passwd,
 is lost at reboot. Permanent defaults: br2-dsl2542b/package/swmirror/
 swmirror.conf in the build tree, then rebuild.

EOF
	swmirror status 2>/dev/null
	echo
fi
