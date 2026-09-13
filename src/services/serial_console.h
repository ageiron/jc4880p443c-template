#pragma once
// Line-based serial command console for entering the AI assistant's API keys over USB serial
// instead of the on-device keyboard. Anthropic/OpenAI keys run 50-100+ characters — typing that
// on a 4.3" on-screen keyboard is impractical, and the board is already tethered to a PC over
// COM6/USB for flashing anyway. See FUNCTIONAL_DESCRIPTION.md "Security / secrets".
//
// Commands (send over the PlatformIO/idf.py serial monitor, one per line, newline-terminated):
//   SET_ANTHROPIC_KEY:<key>
//   SET_OPENAI_KEY:<key>
// Each replies "OK, saved to NVS (<n> chars)" or "ERROR: <reason>".
namespace serial_console {

// Installs the UART driver in interrupt/buffered mode (needed for blocking reads — the default
// boot console UART is write-only/polled) and spawns the reader task. Call once from app_main().
void start();

} // namespace serial_console
