# include <msp430.h>
# include <stdio.h>
# include <stdint.h>

// define global variables
#define num_leds 41
#define data_len 168
#define receive_len 13    // size of array to receive from ESP8266
unsigned int data_idx = 0;
char dummy;

// LED array
unsigned char colorstep = 0x20;           // step for incrementing the colors
char color[] = {0x00, 0x00, 0x00};        // array for storing a RGB color. The order is BGR.
unsigned int lightness = 0xA5;
unsigned char seq[data_len] = {0x00};      // sequence used for computing colors
unsigned char trans_seq[data_len] = {0x00};    // stores the sequence to transmit
long i=0;
char received_ch;    // testing 1 byte

// variables to store received SPI data
uint8_t LEDmode = 0;
float temp;
float eCO2;
float TVOC;


/*
* Sets the global variable color to the perceding color in the spectrum.
* (power conscious HSV)
*/
void prev_color_rainbows() {
    char b = color[0];
    char g = color[1];
    char r = color[2];

        // power conscious HSV
        if (g > 0) {
            if (b == 0) {
                if (g > 0xFF - colorstep) {
                   // checks for overflow
                    color[1] = (0xFF - colorstep) + (0xFF - g);
                    color[0] = 0xFF - color[1];
                    color[2] = 0;
                } else {
                    color[0] = 0;
                    color[1] = g + colorstep;
                    color[2] = 0xFF - color[1];
                }
            } else if (r == 0) {             // green ~ red area
                if (b > 0xFF - colorstep) {
                    // checks for overflow
                    color[1] = 0;
                    color[0] = 0xFF - colorstep + (0xFF - b);
                    color[2] = 0xFF - color[0];
                } else {
                    color[1] = g - colorstep;
                    color[0] = 0xFF - color[1];
                    color[2] = 0;
                }
            }
        } else {
            if (r > 0xff - colorstep) {
                 // checks for overflow
                color[0] = 0;
                color[2] = 0xFF - colorstep + (0xFF - r);
                color[1] = 0xFF - color[2];
            } else {
                color[1] = 0;
                color[0] = b - colorstep;
                color[2] = r + colorstep;
            }
        }
}

/*
* Sets the global variable color to the next color in the spectrum.
* The R and G bits are computed by calling previous color using
* the symmetric property of the HSV graph.
*
*/
void next_color_rainbows() {
    char b = color[0];
    char g = color[1];
    color[0] = g;
    color[1] = b;
    prev_color_rainbows();
    b = color[1];
    g = color[0];
    color[0] = b;
    color[1] = g;
}


/*
* Sets the color sequence for the LEDs to transmit.
* If LEDMode is 1, the rainbow sequence(full spectrum) will be generated.
* If LEDMode is 2, the berry sequence(red, blue, purple) will be generated.
* If LEDMode is 3, the tropical sequence(red, yellow, green) will be generated.
* If LEDMode is 4, the mermaid sequence(blue, green) will be generated. 
*/
void set_sequence() {
    int led_i;
        // forward shift
        for (led_i = num_leds - 1; led_i >=1; led_i--) {
            // set values for each LED
            seq[(led_i + 1) * 4 + 1] = seq[led_i * 4 + 1];
            seq[(led_i + 1) * 4 + 2] = seq[led_i * 4 + 2];
            seq[(led_i + 1) * 4 + 3] = seq[led_i * 4 + 3];


            // update transmitting sequence
            trans_seq[(led_i + 1) * 4 + 1] = trans_seq[led_i * 4 + 1];
            trans_seq[(led_i + 1) * 4 + 2] = trans_seq[led_i * 4 + 2];
            trans_seq[(led_i + 1) * 4 + 3] = trans_seq[led_i * 4 + 3];
        }

        // set values for the bottom LED
        color[0] = seq[2 * 4 + 1];
        color[1] = seq[2 * 4 + 2];
        color[2] = seq[2 * 4 + 3];


        next_color_rainbows();

        // set the new color
        seq[1 * 4 + 1] = color[0];    // b
        seq[1 * 4 + 2] = color[1];    // g
        seq[1 * 4 + 3] = color[2];    // r


        if (LEDmode == 1) {
           // rainbow
          trans_seq[1 * 4 + 1] = color[0];    // b
          trans_seq[1 * 4 + 2] = color[1];    // g
          trans_seq[1 * 4 + 3] = color[2];    // r
        } else if (LEDmode == 2) {
           // berry sequence, disable green
           trans_seq[1 * 4 + 1] = color[0];    // b
           trans_seq[1 * 4 + 2] = 0;           // g
           trans_seq[1 * 4 + 3] = color[2];    // r
        } else if (LEDmode == 3) {
          // tropical sequence, disable blue
          trans_seq[1 * 4 + 1] = 0;    // b
          trans_seq[1 * 4 + 2] = color[1];    // g
          trans_seq[1 * 4 + 3] = color[2];    // r
        } else if (LEDmode == 4) {
            // mermaid
            trans_seq[1 * 4 + 1] = color[0];    // b
            trans_seq[1 * 4 + 2] = color[1];    // g
            trans_seq[1 * 4 + 3] = 0;    // r

        }

}



/*
 *  Initiates the sequences. 
 */

void init_sequence() {
    if (LEDmode == 0) {
        // initiate sequence
        memset(seq, 0, data_len);
        memset(trans_seq, 0, data_len);

        seq[data_len - 4] = 0xFF;
        seq[data_len - 3] =  0xFF;
        seq[data_len - 2] =  0xFF;
        seq[data_len - 1] =  0xFF;

        trans_seq[data_len - 4] = 0xFF;
        trans_seq[data_len - 3] =  0xFF;
        trans_seq[data_len - 2] =  0xFF;
        trans_seq[data_len - 1] =  0xFF;



    } else if (LEDmode == 1 || LEDmode == 2) {
        // initiate sequence
        seq[7] =  0xFF;
        seq[data_len - 5] = 0xFF;
        seq[data_len - 4] = 0xFF;
        seq[data_len - 3] =  0xFF;
        seq[data_len - 2] =  0xFF;
        seq[data_len - 1] =  0xFF;
        trans_seq[7] =  0xFF;
        trans_seq[data_len - 5] = 0xFF;
        trans_seq[data_len - 4] = 0xFF;
        trans_seq[data_len - 3] =  0xFF;
        trans_seq[data_len - 2] =  0xFF;
        trans_seq[data_len - 1] =  0xFF;

    } else if (LEDmode == 3 || LEDmode == 4) {
         seq[5] =  0xFF;
         seq[data_len - 7] = 0xFF;

         seq[data_len - 4] = 0xFF;
         seq[data_len - 3] =  0xFF;
         seq[data_len - 2] =  0xFF;
         seq[data_len - 1] =  0xFF;
         trans_seq[7] =  0xFF;
         trans_seq[data_len - 5] = 0xFF;
         trans_seq[data_len - 4] = 0xFF;
         trans_seq[data_len - 3] =  0xFF;
         trans_seq[data_len - 2] =  0xFF;
         trans_seq[data_len - 1] =  0xFF;

    }
    int row_iter;

    // set lightness
    for (row_iter = 1; row_iter <= num_leds; row_iter++) {
        seq[4* row_iter] = lightness;
        trans_seq[4* row_iter] = lightness;
    }

}


/*
 * Configures all the pins. 
 */
void set_up() {
    // set watchdog timer
    BCSCTL3 |= LFXT1S_2;                      // ACLK = VLO
    WDTCTL = WDT_ADLY_16;                     // WDT 16ms, ACLK, interval timer
    IE1 |= WDTIE;                             // Enable WDT interrupt

    // SPI settings for receiving data from WIFI module 
    P1DIR = BIT0;  // for debugging only; set as output
    P1OUT &= ~BIT0;
    P1SEL = BIT1 + BIT2 + BIT4 + BIT5 + BIT7;    // SIMO
    P1SEL2 = BIT1 + BIT2 + BIT4 + BIT5 + BIT7;
    UCA0CTL1 = UCSWRST;                       // **Put state machine in reset**
    UCA0CTL0 |= UCMSB + UCSYNC;               // 3-pin, 8-bit SPI master; clock is provided by the master
    UCA0CTL1 &= ~UCSWRST;                     // **Initialize USCI state machine**
    IE2 |= UCA0RXIE;                          // Enable USCI0 RX interrupt



    // SPI settings for transmitting LED arrays
    UCB0CTL1 |= UCSWRST;                         // **Initialize USCI state machine**
//    UCB0CTL0 |= UCCKPH + UCMSB + UCMST + UCSYNC; // 3-pin, 8-bit SPI master: TODO: check KPH/KPL
    UCB0CTL0 |= UCMSB + UCMST + UCSYNC;  // 3-pin, 8-bit SPI master: TODO: test this part
    UCB0CTL1 |= UCSSEL_2; // SMCLK
    UCB0BR0 |= 0; // transmission rate
    UCB0BR1 = 0;
    UCB0CTL1 &= ~UCSWRST; // **Initialize USCI state machine**
    IE2 |= UCB0TXIE; // Enable USCI0 TX interrupt
}



int main(void)
{
    init_sequence();
    set_up();
    __bis_SR_register(GIE); // CPU off, enable interrupts


        // transmit the light sequence
        __bis_SR_register(LPM0_bits); // CPU off, enable interrupts
        while (1) {
          __bis_SR_register(LPM0_bits); // CPU off, enable interrupts

          // trigger TX ISR
          UCB0TXBUF = trans_seq[data_idx];

          // if the mode change, reset the color at the end
          if (LEDmode == 0) {
              init_sequence();
          }
          set_sequence();
    }


}


// Watchdog Timer interrupt service routine
#if defined(__TI_COMPILER_VERSION__) || defined(__IAR_SYSTEMS_ICC__)
#pragma vector=WDT_VECTOR
__interrupt void watchdog_timer(void)
#elif defined(__GNUC__)
void __attribute__ ((interrupt(WDT_VECTOR))) watchdog_timer (void)
#else
#error Compiler not supported!
#endif
{
    __bic_SR_register_on_exit(LPM0_bits);
}


// Test for valid TX character
#if defined(__TI_COMPILER_VERSION__) || defined(__IAR_SYSTEMS_ICC__)
#pragma vector=USCIAB0TX_VECTOR
__interrupt void USCIB0TX_ISR(void)
#elif defined(__GNUC__)
void __attribute__ ((interrupt(USCIAB0TX_VECTOR))) USCIB0TX_ISR (void)
#else
#error Compiler not supported!
#endif
{
    while (!(IFG2 & UCB0TXIFG));
    data_idx += 1;
    UCB0TXBUF = trans_seq[data_idx];

    if (data_idx == (data_len - 1)) {
        data_idx = 0;
        IFG2 &= ~UCB0TXIFG;  // finish transmitting the entire sequence
    }
}

// Test for valid RX character
__attribute__((interrupt(USCIAB0RX_VECTOR))) void USCI0RX_ISR (void)
{

  while (!(IFG2 & UCA0TXIFG));
// test: try to receive one byte only
  LEDmode = (uint8_t) UCA0RXBUF;
  LEDmode = LEDmode >> 1;
  IFG2 &= ~UCA0RXIFG;

}



