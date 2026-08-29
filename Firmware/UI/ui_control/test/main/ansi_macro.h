#ifndef ANSI_MACRO_H
#define ANSI_MACRO_H

/* ANSI color escape codes used to prettify ESP_LOG lines on a serial console
 * that supports color (e.g. idf.py monitor). Harmless no-ops on terminals
 * that don't render ANSI escapes -- they'll just show as raw characters. */

#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define WHITE   "\033[37m"

#endif // ANSI_MACRO_H
