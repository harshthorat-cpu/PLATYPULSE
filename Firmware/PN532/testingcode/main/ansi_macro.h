#ifndef _ANSI_MACRO_H_
#define _ANSI_MACRO_H_


// ANSI MACRO
#define RESET           "\33[0m"
#define BOLD            "\33[1m"
#define ITALIC          "\33[3m"
#define UNDERLINE       "\33[4m"

#define COLOR0          "\33[30m"               
#define RED             "\33[31m"               
#define GREEN           "\33[32m"               
#define YELLOW          "\33[33m"               
#define BLUE_DARK       "\33[34m"               
#define PURPLE          "\33[35m"               
#define MAGENTA         "\33[38;2;255;0;255m"   
#define CYAN_CUSTOM     "\33[38;2;6;182;212m"
#define CYAN_256        "\33[38;5;159m"
#define CYAN            "\33[36m"               
#define WHITE           "\33[37m"               
#define ORANGE          "\33[38;5;208m"         
#define LILAC           "\33[38;5;183m"         
#define BLUE            "\33[94m"               
#define BLUE_CUSTOM     "\33[38;2;37;99;235m"

#define PINK            "\33[38;5;200m"         
#define LIGHT_PINK      "\33[38;5;218m"         
#define SALMON          "\33[38;5;209m"         
#define GOLD            "\33[38;5;220m"         
#define LIGHT_ORANGE    "\33[38;5;214m"         
#define LIGHT_YELLOW    "\33[38;5;229m"         
#define OLIVE           "\33[38;5;100m"         
#define LIGHT_GREEN     "\33[38;5;120m"         
#define MINT            "\33[38;5;121m"         
#define TURQUOISE       "\33[38;5;45m"          
#define AQUA            "\33[38;5;51m"          
#define TEAL            "\33[38;5;30m"        
#define INDIGO          "\33[38;5;54m"          
#define VIOLET          "\33[38;5;93m"         
#define LAVENDER        "\33[38;5;183m"         
#define LIGHT_GRAY      "\33[38;5;250m"        
#define DARK_GRAY       "\33[38;5;240m"         

#define BG_BLACK        "\33[40m"             
#define BG_RED          "\33[41m"               
#define BG_GREEN        "\33[42m"             
#define BG_YELLOW       "\33[43m"          
#define BG_BLUE         "\33[44m"              
#define BG_MAGENTA      "\33[45m"            
#define BG_CYAN         "\33[46m"             
#define BG_WHITE        "\33[47m"             

#define BRIGHT_BLACK    "\33[90m"           
#define BRIGHT_RED      "\33[91m"               
#define BRIGHT_GREEN    "\33[92m"             
#define BRIGHT_YELLOW   "\33[93m"              
#define BRIGHT_BLUE     "\33[94m"        
#define BRIGHT_MAGENTA  "\33[95m"            
#define BRIGHT_CYAN     "\33[96m"               
#define BRIGHT_WHITE    "\33[97m"               
#define BG_ORANGE       "\33[48;5;208m"        
#define BG_LILAC        "\33[48;5;183m"        


// Reset all attributes
#define ANSI_RESET "\033[0m"

#define ANSI_RETURN "\r\n"

// Clear terminal screen and move cursor to top-left
#define ANSI_CLEAR "\033[3J\033[H\033[2J"

// Text colors
#define ANSI_BLACK "\033[30m"
#define ANSI_RED "\033[31m"
#define ANSI_GREEN "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_BLUE "\033[34m"
#define ANSI_MAGENTA "\033[35m"
#define ANSI_CYAN "\033[36m"
#define ANSI_WHITE "\033[37m"

// Bright text colors
#define ANSI_BRIGHT_BLACK "\033[90m"
#define ANSI_BRIGHT_RED "\033[91m"
#define ANSI_BRIGHT_GREEN "\033[92m"
#define ANSI_BRIGHT_YELLOW "\033[93m"
#define ANSI_BRIGHT_BLUE "\033[94m"
#define ANSI_BRIGHT_MAGENTA "\033[95m"
#define ANSI_BRIGHT_CYAN "\033[96m"
#define ANSI_BRIGHT_WHITE "\033[97m"

// Background colors
#define ANSI_BG_BLACK "\033[40m"
#define ANSI_BG_RED "\033[41m"
#define ANSI_BG_GREEN "\033[42m"
#define ANSI_BG_YELLOW "\033[43m"
#define ANSI_BG_BLUE "\033[44m"
#define ANSI_BG_MAGENTA "\033[45m"
#define ANSI_BG_CYAN "\033[46m"
#define ANSI_BG_WHITE "\033[47m"

// Bright background colors
#define ANSI_BG_BRIGHT_BLACK "\033[100m"
#define ANSI_BG_BRIGHT_RED "\033[101m"
#define ANSI_BG_BRIGHT_GREEN "\033[102m"
#define ANSI_BG_BRIGHT_YELLOW "\033[103m"
#define ANSI_BG_BRIGHT_BLUE "\033[104m"
#define ANSI_BG_BRIGHT_MAGENTA "\033[105m"
#define ANSI_BG_BRIGHT_CYAN "\033[106m"
#define ANSI_BG_BRIGHT_WHITE "\033[107m"

// Text effects
#define ANSI_BOLD "\033[1m"
#define ANSI_DIM "\033[2m"
#define ANSI_ITALIC "\033[3m"
#define ANSI_UNDERLINE "\033[4m"
#define ANSI_BLINK "\033[5m"
#define ANSI_REVERSE "\033[7m"
#define ANSI_HIDDEN "\033[8m"
#define ANSI_STRIKETHROUGH "\033[9m"


#endif // _ANSI_MACRO_H_