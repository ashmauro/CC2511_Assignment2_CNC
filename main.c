/**************************************************************
 * main.c
 * rev 1.0 22-Oct-2021 Ashley Mauro, Jessica Grimes
 * CC2511 CNC milling-machine operational code
 * Assignment2_AshMauroJessicaGrimes
 * ***********************************************************/

#include "pico/stdlib.h"
#include <stdbool.h>
#include <stdio.h>
#include "hardware/pwm.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "terminal.h"
#include <stdlib.h>
#include <math.h>

// Define GPIO Pins
#define RX_PIN      1
#define TX_PIN      0
#define X_DIR       4
#define X_STEP      3
#define Y_DIR       7
#define Y_STEP      6
#define Z_DIR       10
#define Z_STEP      11
#define SPIN_LOG    15
#define MODE0       20
#define MODE1       19
#define MODE2       18
#define nENBL       16

// Define data types
static int chars_rxed = 0;
volatile char buffer[1000];                                // Buffer for uart
volatile unsigned int buffer_index = 0;                    // Buffer index
volatile bool has_command = false;                         // Uart command
unsigned short background;                                 // terminal colours
unsigned short foreground;                                 // terminal colours
bool manual = true;                                        // Conditional boolean for manual mode (true to enter manual mode when first load)
bool box, sunrise = false;                                 // Conditional boolean for draw_box and draw_bit_sunrise mode
bool first_gcode_run = true;                               // Conditonal boolean for first gcode run
int i = 0;                                                 // Iteration value for stepper loop
int spin_pwm = 0;                                          // Spindle speed pwm
int16_t x_pos, y_pos, z_pos = 0;                           // goal axis positions sent through by user
const char *acceptable_inputs = "xyzsxobitrhm0123456789-"; // string of acceptable user inputs for error checking
int error_code = 0;                                        // integer to classify error codes by
int x_value, y_value = 0;                                  // integer values to pass user input coordinates to dynamic_lines function

/*
########################################################

This function is an RX interupt handler and detects when 
an event has come through. Had various case statment that 
analyse if the enter or backspace button was pressed and 
adjusts buffer and buffer index accordinly. It thens 
calls sench_ch function to send specific character at 
each instance. 

########################################################
*/

void on_uart_rx() {
  while (uart_is_readable(uart0)) {
    uint8_t ch = uart_getc(uart0);
    switch (ch) {
    // Detect enter button pressed
    case '\r':
    case '\n':
      buffer[buffer_index] = 0; // Add trailing null
      has_command = true;       // Enter key has been pressed
      break;

    // detect backspace button pressed
    case 0x7f:
      if (buffer_index > 0) { // check if buffer is empty
        buffer[buffer_index] = '\0';
        buffer_index--;
      }
      send_ch(0x7f);
      break;

    // default send character
    default:
      if (buffer_index < 999) { // check if buffer is full
        buffer[buffer_index] = ch;
        buffer_index++;
      }
      send_ch(ch);
      break;
    }
    chars_rxed++;
  }
}

// Function to write character
void send_ch(char ch) {
  if (uart_is_writable(uart0)) {
    uart_putc(uart0, ch);
  }
}

/*
########################################################

The following various functions create the Gcode mode 
terminal user interface that is displayed on putty.

########################################################
*/

void draw_heading() {
  term_move_to(1, 1);
  term_set_color(clrGreen, clrBlack);
  printf("+----------------------CC2511 Assignment 2----------------------+\r ");
}

void draw_cnc_heading() {
  term_move_to(1, 3);
  term_set_color(clrBlack, clrGreen);
  printf("+-[CNC STATUS]-+\r ");
}

void draw_menu_heading() {
  term_move_to(18, 3);
  term_set_color(clrBlack, clrGreen);
  printf("+--------------------[MENU]--------------------+\r ");
}

void draw_menu() {
  term_move_to(23, 5);
  term_set_color(clrGreen, clrBlack);
  printf("Type the following commands:");
  term_move_to(23, 6);
  printf("> xn         Move x-axis to position n\r\n");
  term_move_to(23, 7);
  printf("> yn         Move y-axis to position n\r\n");
  term_move_to(23, 8);
  printf("> zn         Move z-axis to position n\r\n");
  term_move_to(23, 9);
  printf("> sn         Set spindle speed to PWM\r\n");
  term_move_to(23, 10);
  printf("> h          Move tool back to home position\r\n");
  term_move_to(23, 11);
  printf("> b          Draw auto box at current location\r\n");
  term_move_to(23, 12);
  printf("> bit        Draw BIT SUNRISE at current location\r\n");
  term_move_to(23, 13);
  printf("> m          Exit Gcode mode back to Manual Mode\r\n");
  term_move_to(7, 14);
  printf("Note: Please enter in G-Code format i.e 'xn yn zn sn'\r\n");
  term_move_to(7, 15);
  printf("      Does not need to be all fields i.e 'xn sn'\r\n");
}

void draw_cnc_status(int spin_pwm, int x_pos, int y_pos, int z_pos) {
  term_set_color(clrGreen, clrBlack);
  term_move_to(2, 5);
  printf("Position Status\r\n");
  term_move_to(2, 6);
  term_erase_line();
  printf("> X POS:    %i\r\n", x_pos);
  term_move_to(2, 7);
  term_erase_line();
  printf("> Y POS:    %i\r\n", y_pos);
  term_move_to(2, 8);
  term_erase_line();
  printf("> Z POS:    %i\r\n", z_pos);
  term_move_to(2, 10);
  printf("Spindle Status\r\n");
  term_move_to(2, 11);
  term_erase_line();
  printf("> Spindle:  %i\r\n", spin_pwm);
}

void draw_bottom() {
  term_move_to(1, 16);
  term_set_color(clrBlack, clrGreen);
  printf("+---------------------------------------------------------------+");
}

void print_command() {
  term_move_to(2, 18);
  // term_erase_line();
  term_set_color(clrWhite, clrBlack);
  printf("Command prompt: \r\n");
  term_move_to(35, 18);
  term_erase_line();
  if (error_code == 1) {
    term_set_color(clrRed, clrBlack);
    printf("Error, invalid command entered\r\n");
  }
  else if (error_code == 2) {
    term_set_color(clrRed, clrBlack);
    printf("Error, Z-axis must be 0 before homeing\r\n");
  }
  else if (error_code == 3) {
    term_set_color(clrRed, clrBlack);
    printf("Error, X-axis set position is outside limits\r\n");
  }
  else if (error_code == 4) {
    term_set_color(clrRed, clrBlack);
    printf("Error, Y-axis set position is outside limits\r\n");
  }
  else {
    term_set_color(clrGreen, clrBlack);
    printf("Valid command enetered\r\n");
  }
  term_set_color(clrWhite, clrBlack);
  term_move_to(1, 19);
  term_erase_line();
  printf(">>> ");
}

/*
########################################################

The following function creates the manual mode terminal
user interface that is displayed on putty.

########################################################
*/

void print_manual_mode() {
  term_cls();
  term_set_color(clrGreen, clrBlack);
  term_move_to(1, 1);
  printf("+------------------Manual Mode ON-------------------+\r\n");
  term_set_color(clrBlack, clrGreen);
  term_move_to(1, 3);
  printf("+----------------[Manual Mode Menu]-----------------+");
  term_set_color(clrGreen, clrBlack);
  term_move_to(2, 5);
  printf("> Move X-axis and Y-axis by 'a' 's' 'd' 'w' respectively\r\n");
  term_move_to(2, 6);
  printf("> Move Z-axis by 'r' and 'f'\r\n");
  term_move_to(2, 7);
  printf("> Toggle spindle on and off by 'o' and 'p' respectively\r\n");
  term_move_to(2, 8);
  printf("> Set home position by 'h' at the centre\r\n");
  term_move_to(2, 9);
  printf("> Set X and Y positive limit positions by 'm' at the top right corner\r\n");
  term_move_to(2, 10);
  printf("> Set X and Y negative limit positions by 'm' at the bottom left corner\r\n");
  term_move_to(2, 11);
  printf("> Once process in complete, press 'x' to exit manual\r\n");
  term_set_color(clrBlack, clrGreen);
  term_move_to(1, 13);
  printf("+---------------------------------------------------+\r\n");
  term_set_color(clrGreen, clrBlack);
}

// Create a position object
struct pos {
  int16_t x_element;
  int16_t y_element;
  int16_t z_element;
};

struct pos current_pos = {0, 0, 0};        // Create an current position instance of the position object
struct pos set_pos = {0, 0, 0};            // Create an goal position instance of the position object
struct pos home_pos = {0, 0, 0};           // Creat home position instance for manual mode
struct pos positive_limit_pos = {0, 0, 0}; // Create positive limit position instance for manual mode
struct pos negative_limit_pos = {0, 0, 0}; // Create negative limit position instance for manual mode

// Initialise uart interupt for switch from manual mode to gcode
int initialise_uart() {
  // Initialise UART 0
  uart_init(uart0, 115200);

  // Set to GPIO pin mux to the UART - 0 is TX, 1 is RX
  gpio_set_function(TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(RX_PIN, GPIO_FUNC_UART);

  // Select correct interrupt for the UART
  int UART_IRQ = uart0 == uart0 ? UART0_IRQ : UART1_IRQ;

  // And set up and enable the interrupt handlers
  irq_set_exclusive_handler(UART_IRQ, on_uart_rx);
  irq_set_enabled(UART_IRQ, true);

  // Now enable the UART to send interrupts - RX only
  uart_set_irq_enables(uart0, true, false);

  // Turn off FIFO's
  uart_set_fifo_enabled(uart0, false);
}

// Terminate uart interupt for switch from Gcode to manual mode
int terminate_uart() {
  // Initialise UART 0
  uart_init(uart0, 115200);

  // Set to GPIO pin mux to the UART - 0 is TX, 1 is RX
  gpio_set_function(TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(RX_PIN, GPIO_FUNC_UART);

  // Select correct interrupt for the UART
  int UART_IRQ = uart0 == uart0 ? UART0_IRQ : UART1_IRQ;

  // And set up and enable the interrupt handlers
  irq_set_exclusive_handler(UART_IRQ, on_uart_rx);
  irq_set_enabled(UART_IRQ, false);

  // Now enable the UART to send interrupts - RX only
  uart_set_irq_enables(uart0, false, false);

  // Turn off FIFO's
  uart_set_fifo_enabled(uart0, false);
}

// Main to run CNC manual mode
int manual_mode_main() {
  // Manual mode Menu
  print_manual_mode();
  // Set mode for 1/4 step
  gpio_put(MODE0, false);
  gpio_put(MODE1, true);
  gpio_put(MODE2, false);

  bool valid_input = true; // Local conditional boolean to run manual mode
  while (valid_input) {
    int ch = getchar_timeout_us(0);
    if (ch != PICO_ERROR_TIMEOUT) {
      switch (ch) {
      // Following 2 cases move x-axis manually
      case 'a':
        gpio_put(X_DIR, false);
        for (i = 0; i < 100; i++) {
          gpio_put(X_STEP, true);
          sleep_us(1000);
          gpio_put(X_STEP, false);
          sleep_us(1000);
          x_pos -= 1;
        }
        break;

      case 'd':
        gpio_put(X_DIR, true);
        for (i = 0; i < 100; i++) {
          gpio_put(X_STEP, true);
          sleep_us(1000);
          gpio_put(X_STEP, false);
          sleep_us(1000);
          x_pos += 1;
        }
        break;

      // Following 2 cases move Y-axis manually
      case 's':
        gpio_put(Y_DIR, false);
        for (i = 0; i < 100; i++) {
          gpio_put(Y_STEP, true);
          sleep_us(1000);
          gpio_put(Y_STEP, false);
          sleep_us(1000);
          y_pos += 1;
        }
        break;

      case 'w':
        gpio_put(Y_DIR, true);
        for (i = 0; i < 100; i++) {
          gpio_put(Y_STEP, true);
          sleep_us(1000);
          gpio_put(Y_STEP, false);
          sleep_us(1000);
          y_pos -= 1;
        }
        break;

      // Following 2 cases move Z-axis manually
      case 'r':
        gpio_put(Z_DIR, true);
        for (i = 0; i < 100; i++) {
          gpio_put(Z_STEP, true);
          sleep_us(1000);
          gpio_put(Z_STEP, false);
          sleep_us(1000);
          z_pos += 1;
        }
        break;

      case 'f':
        gpio_put(Z_DIR, false);
        for (i = 0; i < 100; i++) {
          gpio_put(Z_STEP, true);
          sleep_us(1000);
          gpio_put(Z_STEP, false);
          sleep_us(1000);
          z_pos -= 1;
        }
        break;

      // Set home positon
      case 'h':
        x_pos = 0;
        y_pos = 0;
        z_pos = 0;
        home_pos.x_element = x_pos;
        home_pos.y_element = y_pos;
        home_pos.z_element = z_pos;
        term_set_color(clrGreen, clrBlack);
        printf("Home position set\r\n");
        break;

      // Set X and Y positive limit position
      case 'm':
        if ((x_pos <= home_pos.x_element) || (y_pos <= home_pos.y_element)) { // Error check to make sure positive limits are actually positive
          term_set_color(clrRed, clrBlack);
          printf("Error, positive limits must be greater than home position\r\n");
          printf("Current Position: x: %i, y: %i\r\n", x_pos, y_pos);
        }
        else {
          positive_limit_pos.x_element = x_pos;
          positive_limit_pos.y_element = y_pos;
          term_set_color(clrGreen, clrBlack);
          printf("Positive X-axis and Y-axis limits set\r\n");
          printf("Set Position: x: %i, y: %i\r\n", x_pos, y_pos);
        }
        break;

      // Set X and Y negative limit position
      case 'n':
        if ((x_pos >= home_pos.x_element) || (y_pos >= home_pos.y_element)) { // Error check to make sure negative limits are actually negative
          term_set_color(clrRed, clrBlack);
          printf("Error, negative limits must be less than home position\r\n");
          printf("Current Position: x: %i, y: %i\r\n", x_pos, y_pos);
        }
        else {
          negative_limit_pos.x_element = x_pos;
          negative_limit_pos.y_element = y_pos;
          term_set_color(clrGreen, clrBlack);
          printf("Negative X-axis and Y-axis limits set\r\n");
          printf("Set Position: x: %i, y: %i\r\n", x_pos, y_pos);
        }
        break;

      // Toggle spindle on
      case 'o':
        term_set_color(clrGreen, clrBlack);
        printf("Spindle toggled on\r\n");
        pwm_set_gpio_level(SPIN_LOG, 255 * 255);
        break;

      // Toggle spindle off
      case 'p':
        term_set_color(clrGreen, clrBlack);
        printf("Spindle toggled off\r\n");
        pwm_set_gpio_level(SPIN_LOG, 0);
        break;

      // Break out of manula mode to Gcode mode
      case 'x':
        valid_input = false;
        first_gcode_run = true;
        manual = false;
        current_pos.x_element = x_pos; // Set current position to current positions
        current_pos.y_element = y_pos;
        current_pos.z_element = z_pos;
        break;

      // Refresh screen
      case 'q':
        print_manual_mode();
        break;
      }
    }
  }
}

int draw_box() {
  // Set to 1/4 step, bit redundant but just incase
  gpio_put(MODE0, false);
  gpio_put(MODE1, true);
  gpio_put(MODE2, false);
  box = false;
  bool box_input = true;
  // Loop to draw a simple box. This is to showcase how you would feed coorinates into the pico
  while (box_input) {
    dynamic_lines(3000, 0);
    dynamic_lines(0, -3000);
    dynamic_lines(-3000, 0);
    dynamic_lines(0, 3000);
    box_input = false;
  }
}

int draw_bit_sunrise() {
  // Set to 1/4 step, bit redundant but just incase
  gpio_put(MODE0, false);
  gpio_put(MODE1, true);
  gpio_put(MODE2, false);
  sunrise = false;
  bool sunrise_input = true;
  // Loop to showcase how the dynamic lines function can draw any line in any direction based on coordinates
  while (sunrise_input) {
    dynamic_lines(6000, 0);
    dynamic_lines(0, -6000);
    dynamic_lines(-6000, 0);
    dynamic_lines(0, 6000);
    dynamic_lines(6000, -1000);
    dynamic_lines(0, -1000);
    dynamic_lines(-6000, 2000);
    dynamic_lines(6000, -3000);
    dynamic_lines(0, -1000);
    dynamic_lines(-6000, 4000);
    dynamic_lines(6000, -5000);
    dynamic_lines(0, -1000);
    dynamic_lines(-6000, 6000);
    dynamic_lines(5000, -6000);
    dynamic_lines(-1000, 0);
    dynamic_lines(-4000, 6000);
    dynamic_lines(3000, -6000);
    dynamic_lines(-1000, 0);
    dynamic_lines(-2000, 6000);
    dynamic_lines(1000, -6000);
    dynamic_lines(-1000, 0);
    dynamic_lines(0, 6000);
    sunrise_input = false;
  }
}

int dynamic_lines(int x_value, int y_value) { // Function to engrave any lines Bresenham's Line Algorithim
  // Iteration value
  i = 0;
  long over = 0;
  // Change in axis's
  long dx = x_value - current_pos.x_element;
  long dy = y_value - current_pos.y_element;
  // Calculate axis movement direction based on change in each axis
  int dirx = dx > 0 ? 1 : -1;
  int diry = dy > 0 ? -1 : 1;
  // Get absolute values to compare in loop
  dx = abs(dx);
  dy = abs(dy);

  if (dx > dy)
  { // More change in x than y
    over = dx / 2;
    for (i = 0; i < dx; ++i)
    { // loop through the max amount in change
      // Set axis directions and step
      if (dirx == 1)
      {
        gpio_put(X_DIR, true);
        gpio_put(X_STEP, true);
      }
      else
      {
        gpio_put(X_DIR, false);
        gpio_put(X_STEP, true);
      }
      over += dy;
      if (over >= dx)
      {
        over -= dx;
        // Set axis directions and step
        if (diry == 1)
        {
          gpio_put(Y_DIR, true);
          gpio_put(Y_STEP, true);
        }
        else
        {
          gpio_put(Y_DIR, false);
          gpio_put(Y_STEP, true);
        }
      }
      // Set all step to low
      sleep_us(1000);
      gpio_put(X_STEP, false);
      gpio_put(Y_STEP, false);
      sleep_us(1000);
    }
  }
  else
  { // More change in x than y
    over = dy / 2;
    for (i = 0; i < dy; ++i)
    {
      // Set axis directions and step
      if (diry == 1)
      {
        gpio_put(Y_DIR, true);
        gpio_put(Y_STEP, true);
      }
      else
      {
        gpio_put(Y_DIR, false);
        gpio_put(Y_STEP, true);
      }
      over += dx;
      if (over >= dy)
      {
        over -= dy;
        // Set axis directions and step
        if (dirx == 1)
        {
          gpio_put(X_DIR, true);
          gpio_put(X_STEP, true);
        }
        else
        {
          gpio_put(X_DIR, false);
          gpio_put(X_STEP, true);
        }
      }
      // Set all step to low
      sleep_us(1000);
      gpio_put(X_STEP, false);
      gpio_put(Y_STEP, false);
      sleep_us(1000);
    }
  }
  return;
}

int gcode_main() {
  // Call interface functions
  draw_heading();
  draw_bottom();
  draw_cnc_heading();
  draw_menu_heading();
  draw_cnc_status(spin_pwm, x_pos, y_pos, z_pos);
  draw_menu();
  print_command();

  // Set error back to zero
  error_code = 0;

  while (!has_command) { // Does nothing while no command
    __asm("wfi");
  }

  if (has_command == true) {

    char *axis_element = strtok(buffer, " "); // Split buffer at the spaces
    while ((axis_element != NULL) && (error_code == 0)) {
      char *c = axis_element;
      while (*c) {
        if (!strchr(acceptable_inputs, *c)) { // Check character is in acceptable inputs
          error_code = 1;
          term_set_color(clrRed, clrBlack);
          term_move_to(35, 18);
          term_erase_line();
          printf("Error, invalid command entered\r\n");
          break;
        }
        term_set_color(clrGreen, clrBlack);
        term_move_to(35, 18);
        term_erase_line();
        printf("Valid command entered\r\n");
        c++;
      }

      if (sscanf(axis_element, "x%hu", &x_pos) == 1) {
        set_pos.x_element = x_pos; // Assign entered value to respective element in goal position instance
      }

      if (sscanf(axis_element, "y%hu", &y_pos) == 1) {
        set_pos.y_element = y_pos; // Assign entered value to respective element in goal position instance
      }

      if (sscanf(axis_element, "z%hu", &z_pos) == 1) {
        set_pos.z_element = z_pos; // Assign entered value ias respective element in goal position instance
      }

      if (sscanf(axis_element, "s%hu", &spin_pwm) == 1) {
        pwm_set_gpio_level(SPIN_LOG, spin_pwm * spin_pwm); // Set spindle PWM to specifie PWM
      }

      if (0 == strcmp(buffer, "h")) { // Move back to home position
        if (current_pos.z_element >= 0) {
          x_pos = 0;
          y_pos = 0;
          z_pos = 0;
          set_pos.x_element = home_pos.x_element;
          set_pos.y_element = home_pos.y_element;
        }
        else { // Error to stop user from homing with drill still lowered
          error_code = 2;
          term_set_color(clrRed, clrBlack);
          term_move_to(35, 18);
          term_erase_line();
          printf("Error, Z-axis must be 0 before homeing\r\n");
        }
      }

      if (0 == strcmp(buffer, "m")) { // Change back to manual mode, set manual back to true
        manual = true;
        // Weird workaround to clear the buffer if the manual mode is called to break infinite loop.
        has_command = false;
        buffer_index = 0;
        buffer[buffer_index] = '\0';
        return;
      }

      /*
      ##############################################################################################
      The folling two commands to present to simulate a real world application. The idea is to 
      read a text file and feed the pico microcontroller Gcode coordinates on a loop basis to 
      create whatever shape or pattern the user would like.
      ##############################################################################################
       */
      if (0 == strcmp(buffer, "b")) { // Draw box automatically (This will simulate a real world application by feeding the pico with gcode coordinates)
        box = true;
        // Weird workaround to clear the buffer if the box is called to break infinite loop.
        has_command = false;
        buffer_index = 0;
        buffer[buffer_index] = '\0';
        return;
      }

      if (0 == strcmp(buffer, "bit")) { // Draw bit sunrise automatically (This will simulate a real world application by feeding the pico with gcode coordinates)
        sunrise = true;
        // Weird workaround to clear the buffer if the bit sunrise is called to break infinite loop.
        has_command = false;
        buffer_index = 0;
        buffer[buffer_index] = '\0';
        return;
      }
      axis_element = strtok(NULL, " "); // Retrieve next element of G-Code - NULL is input to keep original string
    }

    if (error_code == 0) { // If no invalid inputs occur

      // Set step to hard value. This could also be set up for user implementation.
      gpio_put(MODE0, false);
      gpio_put(MODE1, true);
      gpio_put(MODE2, false);

      if ((current_pos.z_element - set_pos.z_element) < 0) { // Set direction of z axis based on input
        gpio_put(Z_DIR, true);
      }
      else {
        gpio_put(Z_DIR, false);
      }

      for (i = 0; i < (abs(current_pos.z_element - set_pos.z_element)); ++i) { // Step z_axis
        gpio_put(Z_STEP, true);
        sleep_us(1000);
        gpio_put(Z_STEP, false);
        sleep_us(1000);
      }

      // Error code if input coordinates are outside X-axis limits
      if ((set_pos.x_element < negative_limit_pos.x_element) || (set_pos.x_element > positive_limit_pos.x_element)) {
        error_code = 3;
        term_set_color(clrRed, clrBlack);
        term_move_to(35, 18);
        term_erase_line();
        printf("Error, X-axis set position is outside limits\r\n");
        set_pos.x_element = current_pos.x_element;
        x_pos = current_pos.x_element;
      }
      // Error code if input coordinates are outside Y-axis limits
      else if ((set_pos.y_element < negative_limit_pos.y_element) || (set_pos.y_element > positive_limit_pos.y_element)) {
        error_code = 4;
        term_set_color(clrRed, clrBlack);
        term_move_to(35, 18);
        term_erase_line();
        printf("Error, Y-axis set position is outside limits\r\n");
        set_pos.y_element = current_pos.y_element;
        y_pos = current_pos.y_element;
      }
      else {
        // Set dynamic_lines inputs to set posiion values from user input
        x_value = set_pos.x_element;
        y_value = set_pos.y_element;
        dynamic_lines(x_value, y_value);
      }
    }
    // Set goal position to current position
    current_pos = set_pos;
  }
  // Set command back to false
  has_command = false;
  // Set buffer index back to 0
  buffer_index = 0;
  // Set all directions back to false
  gpio_put(X_DIR, false);
  gpio_put(Y_DIR, false);
  gpio_put(Z_DIR, false);
}

int main(void) {
  // Initialise library
  stdio_init_all();

  // Initialise Pins
  gpio_init(X_DIR);
  gpio_init(X_STEP);
  gpio_init(Y_DIR);
  gpio_init(Y_STEP);
  gpio_init(Z_DIR);
  gpio_init(Z_STEP);
  gpio_init(SPIN_LOG);
  gpio_init(MODE0);
  gpio_init(MODE1);
  gpio_init(MODE2);
  gpio_init(nENBL);

  // Set Pin Directions
  gpio_set_dir(X_DIR, true);
  gpio_set_dir(X_STEP, true);
  gpio_set_dir(Y_DIR, true);
  gpio_set_dir(Y_STEP, true);
  gpio_set_dir(Z_DIR, true);
  gpio_set_dir(Z_STEP, true);
  gpio_set_dir(SPIN_LOG, true);
  gpio_set_dir(MODE0, true);
  gpio_set_dir(MODE1, true);
  gpio_set_dir(MODE2, true);
  gpio_set_dir(nENBL, true);

  // Set PWM Function
  gpio_set_function(SPIN_LOG, GPIO_FUNC_PWM);

  // Slice Number
  uint spinslice_num = pwm_gpio_to_slice_num(SPIN_LOG);

  //Enable
  pwm_set_enabled(spinslice_num, true);

  //Set Mode for Micro-stepping
  gpio_put(MODE0, false);
  gpio_put(MODE1, false);
  gpio_put(MODE2, false);

  while (true) {

    if (manual) {
      terminate_uart(); // If manual is true, go to manual mode main
      manual_mode_main();
    }

    if (box) { // If box is true, go to draw box function
      draw_box();
    }

    if (sunrise) { // If sunrise is true, call draw_bit_sunrise function
      draw_bit_sunrise();
    }

    if (first_gcode_run) { // If the first gcode run, initialis the uart. This happens everytime when switching from manual to gcode mode.
      first_gcode_run = false;
      initialise_uart();
      term_cls();
    }
    else { // If already in Gcode mode
      gcode_main();
    }
  }
}
