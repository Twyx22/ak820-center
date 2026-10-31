/*
 * keycodes.cpp — noms français des codes usage HID keyboard (0x04..0xE7)
 */
#include "keycodes.h"

namespace ak820 {

QString hidKeyName(int code) {
    static const struct { int c; const char *n; } tbl[] = {
        { 0x04, "A" }, { 0x05, "B" }, { 0x06, "C" }, { 0x07, "D" }, { 0x08, "E" },
        { 0x09, "F" }, { 0x0A, "G" }, { 0x0B, "H" }, { 0x0C, "I" }, { 0x0D, "J" },
        { 0x0E, "K" }, { 0x0F, "L" }, { 0x10, "M" }, { 0x11, "N" }, { 0x12, "O" },
        { 0x13, "P" }, { 0x14, "Q" }, { 0x15, "R" }, { 0x16, "S" }, { 0x17, "T" },
        { 0x18, "U" }, { 0x19, "V" }, { 0x1A, "W" }, { 0x1B, "X" }, { 0x1C, "Y" },
        { 0x1D, "Z" },
        { 0x1E, "1" }, { 0x1F, "2" }, { 0x20, "3" }, { 0x21, "4" }, { 0x22, "5" },
        { 0x23, "6" }, { 0x24, "7" }, { 0x25, "8" }, { 0x26, "9" }, { 0x27, "0" },
        { 0x28, "Enter" }, { 0x29, "Esc" }, { 0x2A, "Backspace" }, { 0x2B, "Tab" },
        { 0x2C, "Space" }, { 0x2D, "-" }, { 0x2E, "=" }, { 0x2F, "[" }, { 0x30, "]" },
        { 0x31, "\\" }, { 0x32, "#" }, { 0x33, ";" }, { 0x34, "'" }, { 0x35, "`" },
        { 0x36, "," }, { 0x37, "." }, { 0x38, "/" }, { 0x39, "Caps Lock" },
        { 0x3A, "F1" }, { 0x3B, "F2" }, { 0x3C, "F3" }, { 0x3D, "F4" }, { 0x3E, "F5" },
        { 0x3F, "F6" }, { 0x40, "F7" }, { 0x41, "F8" }, { 0x42, "F9" }, { 0x43, "F10" },
        { 0x44, "F11" }, { 0x45, "F12" },
        { 0x46, "Print Screen" }, { 0x47, "Scroll Lock" }, { 0x48, "Pause" },
        { 0x49, "Insert" }, { 0x4A, "Home" }, { 0x4B, "PgUp" },
        { 0x4C, "Delete" }, { 0x4D, "End" }, { 0x4E, "PgDn" },
        { 0x4F, "→" }, { 0x50, "←" }, { 0x51, "↓" }, { 0x52, "↑" },
        { 0x53, "Num Lock" }, { 0x54, "NP /" }, { 0x55, "NP *" },
        { 0x56, "NP -" }, { 0x57, "NP +" }, { 0x58, "NP Enter" },
        { 0x59, "NP 1" }, { 0x5A, "NP 2" }, { 0x5B, "NP 3" },
        { 0x5C, "NP 4" }, { 0x5D, "NP 5" }, { 0x5E, "NP 6" },
        { 0x5F, "NP 7" }, { 0x60, "NP 8" }, { 0x61, "NP 9" },
        { 0x62, "NP 0" }, { 0x63, "NP ." },
        { 0xE0, "L Ctrl" }, { 0xE1, "L Shift" }, { 0xE2, "L Alt" }, { 0xE3, "L Win" },
        { 0xE4, "R Ctrl" }, { 0xE5, "R Shift" }, { 0xE6, "R Alt" }, { 0xE7, "R Win" },
        { 0xAF, "Fn" },
    };
    for (const auto &e : tbl)
        if (e.c == code) return QString::fromUtf8(e.n);
    return QStringLiteral("0x%1").arg(code, 2, 16, QChar('0'));
}

QString consumerKeyName(int code) {
    static const struct { int c; const char *n; } tbl[] = {
        { 0xB5, "Next Track" }, { 0xB6, "Previous Track" }, { 0xB7, "Stop" },
        { 0xCD, "Play / Pause" }, { 0xE2, "Mute" }, { 0xE9, "Volume Up" },
        { 0xEA, "Volume Down" }, { 0x92, "Calculator" }, { 0x9C, "Mail" },
        { 0x83, "Sleep" }, { 0x82, "Reset" }, { 0x81, "Power" },
        { 0x23, "Home" }, { 0x27, "Refresh" }, { 0x8A, "Email" },
    };
    for (const auto &e : tbl)
        if (e.c == code) return QString::fromUtf8(e.n);
    return QStringLiteral("Consumer 0x%1").arg(code, 2, 16, QChar('0'));
}

} /* namespace ak820 */
