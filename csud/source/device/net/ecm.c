/******************************************************************************
*	device/net/ecm.c
*
*	USB CDC-ECM (Ethernet Control Model) network interface driver.
******************************************************************************/

#include <device/net/ecm.h>
#include <hcd/hcd.h>
#include <platform/platform.h>
#include <usbd/descriptors.h>
#include <usbd/usbd.h>

static struct UsbDevice *net_usb_dev = NULL;
static struct UsbPipeAddress rx_pipe;
static struct UsbPipeAddress tx_pipe;
static u8 dev_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
static bool is_attached = false;

static u8 HexNibble(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return 0;
}

void NetLoad(void) {
	InterfaceClassAttach[InterfaceClassCommunications] = CdcEcmAttach;
}

bool CdcEcmIsAttached(void) {
	return is_attached;
}

void CdcEcmGetMac(u8 mac[6]) {
	for (int i = 0; i < 6; i++) {
		mac[i] = dev_mac[i];
	}
}

Result CdcEcmAttach(struct UsbDevice *device, u32 interfaceNumber) {
	u32 dataIface = 0xffffffff;
	u8 epIn = 0, epOut = 0;
	u16 inMax = 64, outMax = 64;
	bool foundIn = false, foundOut = false;

	// Check if this is Ethernet Networking Control Model (Subclass 6) or QEMU network device
	if (device->Interfaces[interfaceNumber].SubClass != 6 &&
	    !(device->Descriptor.VendorId == 0x0525 && device->Descriptor.ProductId == 0xa4a2)) {
		LOGF("ECM: Ignored interface %d with subclass %x.\n", interfaceNumber, device->Interfaces[interfaceNumber].SubClass);
		return ErrorIncompatible;
	}

	// Find the CDC Data Interface (Class 0x0A)
	for (u32 i = 0; i < device->Configuration.InterfaceCount && i < MaxInterfacesPerDevice; i++) {
		if (device->Interfaces[i].Class == InterfaceClassCdcData) {
			dataIface = i;
			break;
		}
	}

	if (dataIface == 0xffffffff) {
		LOGF("ECM: Could not find CDC Data interface on %s.\n", UsbGetDescription(device));
		return ErrorIncompatible;
	}

	// Locate Bulk IN and Bulk OUT endpoints on the data interface
	for (u32 ep = 0; ep < device->Interfaces[dataIface].EndpointCount && ep < MaxEndpointsPerDevice; ep++) {
		volatile struct UsbEndpointDescriptor *epDesc = &device->Endpoints[dataIface][ep];
		if (epDesc->EndpointAddress.Direction == In) {
			epIn = epDesc->EndpointAddress.Number;
			inMax = epDesc->Packet.MaxSize;
			foundIn = true;
		} else {
			epOut = epDesc->EndpointAddress.Number;
			outMax = epDesc->Packet.MaxSize;
			foundOut = true;
		}
	}

	if (!foundIn || !foundOut) {
		LOGF("ECM: Missing data endpoints on %s (IN=%d, OUT=%d).\n", UsbGetDescription(device), foundIn, foundOut);
		return ErrorIncompatible;
	}

	if (inMax == 0) inMax = 64;
	if (outMax == 0) outMax = 64;

	rx_pipe.Type = Bulk;
	rx_pipe.Speed = device->Speed;
	rx_pipe.EndPoint = epIn;
	rx_pipe.Device = device->Number;
	rx_pipe.Direction = In;
	rx_pipe.MaxSize = SizeFromNumber(inMax);

	tx_pipe.Type = Bulk;
	tx_pipe.Speed = device->Speed;
	tx_pipe.EndPoint = epOut;
	tx_pipe.Device = device->Number;
	tx_pipe.Direction = Out;
	tx_pipe.MaxSize = SizeFromNumber(outMax);

	net_usb_dev = device;

	// Parse MAC address from string descriptor if provided
	if (device->Descriptor.SerialNumber != 0) {
		char *macStr = MemoryAllocate(64);
		if (macStr != NULL) {
			if (UsbReadString(device, device->Descriptor.SerialNumber, macStr, 64) == OK) {
				// Serial string format in QEMU is usually MAC or standard string
				// If 12 hex chars: convert to MAC
				u32 len = 0;
				while (macStr[len] != '\0') len++;
				if (len >= 12) {
					for (int i = 0; i < 6; i++) {
						dev_mac[i] = (HexNibble(macStr[i * 2]) << 4) | HexNibble(macStr[i * 2 + 1]);
					}
				}
			}
			MemoryDeallocate(macStr);
		}
	}

	is_attached = true;
	LOGF("ECM: Ethernet adapter ready! Endpoints: IN=%d (max %d), OUT=%d (max %d).\n",
		epIn, inMax, epOut, outMax);
	LOGF("ECM: MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n",
		dev_mac[0], dev_mac[1], dev_mac[2], dev_mac[3], dev_mac[4], dev_mac[5]);

	return OK;
}

Result CdcEcmTx(const void *buffer, u32 length) {
	if (!is_attached || net_usb_dev == NULL) return ErrorDisconnected;
	u32 transferred = 0;
	return HcdSubmitBulkMessage(net_usb_dev, tx_pipe, (void*)buffer, length, &transferred);
}

Result CdcEcmRx(void *buffer, u32 maxLength, u32 *transferred) {
	if (!is_attached || net_usb_dev == NULL) return ErrorDisconnected;
	u32 reqLen = 64; // Read 1 max packet (64 bytes) for full-speed endpoint
	if (maxLength < reqLen) reqLen = maxLength;
	return HcdSubmitBulkMessage(net_usb_dev, rx_pipe, buffer, reqLen, transferred);
}
