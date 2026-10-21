#ifndef __BRIDGE_PROTOCOL_H__
#define __BRIDGE_PROTOCOL_H__

#include <stdint.h>

#define BRIDGE_MAGIC                         0xA5u
#define BRIDGE_REPORT_SIZE                   64u
#define BRIDGE_HEADER_SIZE                   6u
#define BRIDGE_MAX_PAYLOAD                   (BRIDGE_REPORT_SIZE - BRIDGE_HEADER_SIZE)

#define BRIDGE_STATUS_OK                     0x00u
#define BRIDGE_STATUS_BAD_MAGIC              0x01u
#define BRIDGE_STATUS_BAD_COMMAND            0x02u
#define BRIDGE_STATUS_BAD_PAYLOAD            0x03u
#define BRIDGE_STATUS_IO_ERROR               0x04u
#define BRIDGE_STATUS_TIMEOUT                0x05u
#define BRIDGE_STATUS_NOT_READY              0x06u
#define BRIDGE_STATUS_BUSY                   0x07u
#define BRIDGE_STATUS_UNSUPPORTED            0x08u

#define BRIDGE_CMD_PING                      0x01u
#define BRIDGE_CMD_GET_INFO                  0x02u
#define BRIDGE_CMD_RESET_MCU                 0x03u
#define BRIDGE_CMD_ENTER_IAP                 0x04u

#define BRIDGE_CMD_SGPIO_CONFIG                      0x74u
#define BRIDGE_CMD_SGPIO_APPLY                       0x75u
#define BRIDGE_CMD_SGPIO_STATUS                      0x76u
#define BRIDGE_CMD_SGPIO_OFF                         0x77u
#define BRIDGE_CMD_SGPIO_MONITOR_CONFIG              0x78u
#define BRIDGE_CMD_SGPIO_MONITOR_ARM                 0x79u
#define BRIDGE_CMD_SGPIO_MONITOR_STATUS              0x7Au
#define BRIDGE_CMD_SGPIO_MONITOR_STOP                0x7Bu

#endif /* __BRIDGE_PROTOCOL_H__ */
