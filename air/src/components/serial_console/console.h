#ifndef CONSOLE_H
#define CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

// Initializes the interactive console (REPL) over UART.
// This starts a background task which handles user input.
void initialize_console(void);

#ifdef __cplusplus
}
#endif

#endif // CONSOLE_H


