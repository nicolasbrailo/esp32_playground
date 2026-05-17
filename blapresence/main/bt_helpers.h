#pragma once

#include "host/ble_hs.h"

/* Maps a NimBLE disconnect reason (as delivered in ble_gap_event.disconnect.reason)
 * to a short human-readable string. NimBLE wraps HCI errors with a base of 0x200,
 * so a raw HCI code of 0x13 arrives as 0x213. We accept either form.
 * https://mynewt.apache.org/latest/network/ble_hs/ble_hs_return_codes.html */
inline static const char *bt_hci_disconnect_reason_str(int reason) {
  int hci =
      (reason >= BLE_HS_ERR_HCI_BASE && reason < BLE_HS_ERR_HCI_BASE + 0x100) ? reason - BLE_HS_ERR_HCI_BASE : reason;
  switch (hci) {
  case 0x00:
    return "Success";
  case 0x01:
    return "Unknown HCI Command";
  case 0x02:
    return "Unknown Connection Identifier";
  case 0x03:
    return "Hardware Failure";
  case 0x04:
    return "Page Timeout";
  case 0x05:
    return "Authentication Failure";
  case 0x06:
    return "PIN or Key Missing";
  case 0x07:
    return "Memory Capacity Exceeded";
  case 0x08:
    return "Connection Timeout";
  case 0x09:
    return "Connection Limit Exceeded";
  case 0x0a:
    return "Synchronous Connection Limit Exceeded";
  case 0x0b:
    return "Connection Already Exists";
  case 0x0c:
    return "Command Disallowed";
  case 0x0d:
    return "Connection Rejected: Limited Resources";
  case 0x0e:
    return "Connection Rejected: Security Reasons";
  case 0x0f:
    return "Connection Rejected: Unacceptable BD_ADDR";
  case 0x10:
    return "Connection Accept Timeout Exceeded";
  case 0x11:
    return "Unsupported Feature or Parameter Value";
  case 0x12:
    return "Invalid HCI Command Parameters";
  case 0x13:
    return "Remote User Terminated Connection";
  case 0x14:
    return "Remote Device Terminated: Low Resources";
  case 0x15:
    return "Remote Device Terminated: Power Off";
  case 0x16:
    return "Connection Terminated by Local Host";
  case 0x17:
    return "Repeated Attempts";
  case 0x18:
    return "Pairing Not Allowed";
  case 0x19:
    return "Unknown LMP PDU";
  case 0x1a:
    return "Unsupported Remote Feature";
  case 0x1b:
    return "SCO Offset Rejected";
  case 0x1c:
    return "SCO Interval Rejected";
  case 0x1d:
    return "SCO Air Mode Rejected";
  case 0x1e:
    return "Invalid LMP/LL Parameters";
  case 0x1f:
    return "Unspecified Error";
  case 0x20:
    return "Unsupported LMP/LL Parameter Value";
  case 0x21:
    return "Role Change Not Allowed";
  case 0x22:
    return "LMP/LL Response Timeout";
  case 0x23:
    return "LMP Error Transaction Collision";
  case 0x24:
    return "LMP PDU Not Allowed";
  case 0x25:
    return "Encryption Mode Not Acceptable";
  case 0x26:
    return "Link Key Cannot Be Changed";
  case 0x27:
    return "Requested QoS Not Supported";
  case 0x28:
    return "Instant Passed";
  case 0x29:
    return "Pairing With Unit Key Not Supported";
  case 0x2a:
    return "Different Transaction Collision";
  case 0x2c:
    return "QoS Unacceptable Parameter";
  case 0x2d:
    return "QoS Rejected";
  case 0x2e:
    return "Channel Classification Not Supported";
  case 0x2f:
    return "Insufficient Security";
  case 0x30:
    return "Parameter Out Of Mandatory Range";
  case 0x32:
    return "Role Switch Pending";
  case 0x34:
    return "Reserved Slot Violation";
  case 0x35:
    return "Role Switch Failed";
  case 0x36:
    return "Extended Inquiry Response Too Large";
  case 0x37:
    return "Secure Simple Pairing Not Supported By Host";
  case 0x38:
    return "Host Busy - Pairing";
  case 0x39:
    return "Connection Rejected: No Suitable Channel";
  case 0x3a:
    return "Controller Busy";
  case 0x3b:
    return "Unacceptable Connection Parameters";
  case 0x3c:
    return "Advertising Timeout";
  case 0x3d:
    return "Connection Terminated: MIC Failure";
  case 0x3e:
    return "Connection Failed To Be Established";
  case 0x3f:
    return "MAC Connection Failed";
  case 0x40:
    return "Coarse Clock Adjustment Rejected";
  case 0x41:
    return "Type0 Submap Not Defined";
  case 0x42:
    return "Unknown Advertising Identifier";
  case 0x43:
    return "Limit Reached";
  case 0x44:
    return "Operation Cancelled By Host";
  case 0x45:
    return "Packet Too Long";
  default:
    return "Unknown";
  }
}

/* Maps a NimBLE ATT status (e.g. ble_gap_event.notify_tx.status, subscribe.reason,
 * GATT proc results) to a short human-readable string. NimBLE wraps ATT errors
 * with a base of 0x100, so a raw ATT code of 0x0a arrives as 0x10a. We accept
 * either form. Reserved/profile-specific ranges fall through to "Unknown". */
inline static const char *bt_att_err_str(int status) {
  int att =
      (status >= BLE_HS_ERR_ATT_BASE && status < BLE_HS_ERR_ATT_BASE + 0x100) ? status - BLE_HS_ERR_ATT_BASE : status;
  switch (att) {
  case 0x00:
    return "Success";
  case 0x01:
    return "Invalid Handle";
  case 0x02:
    return "Read Not Permitted";
  case 0x03:
    return "Write Not Permitted";
  case 0x04:
    return "Invalid PDU";
  case 0x05:
    return "Insufficient Authentication";
  case 0x06:
    return "Request Not Supported";
  case 0x07:
    return "Invalid Offset";
  case 0x08:
    return "Insufficient Authorization";
  case 0x09:
    return "Prepare Queue Full";
  case 0x0a:
    return "Attribute Not Found";
  case 0x0b:
    return "Attribute Not Long";
  case 0x0c:
    return "Insufficient Encryption Key Size";
  case 0x0d:
    return "Invalid Attribute Value Length";
  case 0x0e:
    return "Unlikely Error";
  case 0x0f:
    return "Insufficient Encryption";
  case 0x10:
    return "Unsupported Group Type";
  case 0x11:
    return "Insufficient Resources";
  case 0x12:
    return "Database Out Of Sync";
  case 0x13:
    return "Value Not Allowed";
  default:
    return "Unknown";
  }
}
