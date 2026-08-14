Import("env")

board_config = env.BoardConfig()

# Microchip USB VID/PID sublicense assignment:
#   VID: 0x04D8
#   PID: 0xE430
#   Request: #5027
# Use is limited to products containing Microchip microcontrollers and is
# subject to the Microchip Universal Serial Bus Vendor ID Sublicense agreement.
#
# Only use VID/PID values legally assigned to this project.
USB_VID = "0x04D8"
USB_PID = "0xE430"

if "XXXX" in USB_VID or "XXXX" in USB_PID:
    raise Exception("Edit custom_hwids.py and set your assigned USB_VID and USB_PID first.")

board_config.update("vendor", "Hanabi SRL")
board_config.update("build.usb_product", "Polaxis Kraftor")

# Must be an array of VID:PID pairs.
board_config.update("build.hwids", [[USB_VID, USB_PID]])

print("Custom USB descriptors enabled:")
print("  VID:PID:", USB_VID, USB_PID)
