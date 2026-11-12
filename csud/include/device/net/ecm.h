/******************************************************************************
*	device/net/ecm.h
*
*	USB CDC-ECM (Ethernet Control Model) network interface driver.
******************************************************************************/

#ifndef _DEVICE_NET_ECM_H
#define _DEVICE_NET_ECM_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <usbd/device.h>
#include <types.h>

#define DeviceDriverEcm 0x4E455431 /* "NET1" */

void NetLoad(void);
Result CdcEcmAttach(struct UsbDevice *device, u32 interfaceNumber);
bool CdcEcmIsAttached(void);
void CdcEcmGetMac(u8 mac[6]);
Result CdcEcmTx(const void *buffer, u32 length);
Result CdcEcmRx(void *buffer, u32 maxLength, u32 *transferred);

#ifdef __cplusplus
}
#endif

#endif
